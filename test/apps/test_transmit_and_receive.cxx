/**
 *
 * @file test_transmit_and_receive.cxx
 *
 * This is a standalone program which will transmit ethernet packets
 * out on one dpdk-enabled network interface port and receive them on
 * another. Obviously you actually need two such ports for this to
 * work.
 *
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"

#include "detdataformats/DAQEthHeader.hpp"
#include "logging/Logging.hpp"

#include "rte_common.h"
#include "rte_eal.h"
#include "rte_ethdev.h"

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include <limits>
#include <mutex>

using namespace dunedaq::dpdklibs;
using dunedaq::detdataformats::DAQEthHeader;

namespace {

  constexpr int burst_size = 256;

  std::mutex lcore_thread_mutex;

  int payload_bytes = 9000;

  std::atomic<long> num_sent_bursts = 0;
  std::atomic<long> num_sent_packets = 0;
  std::atomic<long> num_received_packets = 0;
  std::atomic<long> num_sent_bytes = 0;
  std::atomic<long> num_received_bytes = 0;
  std::atomic<long> num_bad_packets = 0;

}; // namespace

struct lcore_thread_arg {
  rte_mempool* pool;
  int iface_id;
  bool is_receiver;
};

inline bool operator!=(const DAQEthHeader& header1, const DAQEthHeader& header2) {
  return (header1.version != header2.version ||
	  header1.det_id != header2.det_id ||
	  header1.crate_id != header2.crate_id ||
	  header1.slot_id != header2.slot_id ||
	  header1.stream_id != header2.stream_id ||
	  header1.seq_id != header2.seq_id ||
	  header1.block_length != header2.block_length ||
	  header1.timestamp != header2.timestamp);
}


int lcore_main(void* arg) {
  
  uint16_t lid = rte_lcore_id();
  
  // 0: main thread (doesn't use this function), 1: receiver thread, 2: transmitter thread
  if (lid == 0 || lid >= 3) {
    return 0;
  }

  TLOG() << "From lcore_main, lid == " << lid << ", rte_socket_id() == " << rte_socket_id();
  
  lcore_thread_arg info;
  const auto ptr_to_vector_of_thread_configs = static_cast<std::vector<lcore_thread_arg>*>(arg);
  
  {
    std::lock_guard<std::mutex> guard(lcore_thread_mutex);
    info = ptr_to_vector_of_thread_configs->at(lid - 1);
  }
  TLOG() << "In lcore_main, thread #" << lid << ", iface_id == " << info.iface_id << ", is_receiver == " << info.is_receiver;
  
  if (rte_eth_dev_socket_id(info.iface_id) >= 0 && rte_eth_dev_socket_id(info.iface_id) != static_cast<int>(rte_socket_id())) {
    TLOG(TLVL_WARNING) << "WARNING, dpdk interface " << info.iface_id << " is on remote NUMA node to polling thread.\nrte_eth_dev_socket_id(" << info.iface_id << ") == " << rte_eth_dev_socket_id(info.iface_id) << ", rte_socket_id() == " << static_cast<int>(rte_socket_id()) << "\nPerformance will not be optimal.\n";
  }
  
  auto bufs = static_cast<rte_mbuf**>( malloc(sizeof(struct rte_mbuf*) * burst_size) );
  
  if (bufs == NULL) {
    TLOG(TLVL_ERROR) << "Unable to allocate memory for buffers; returning from thread...";
    return 1;
  }

  int retval = 0;
  retval = rte_pktmbuf_alloc_bulk(info.pool, bufs, burst_size);

  if (retval != 0) {
    std::stringstream errstr;
    errstr << "A failure occurred calling rte_pktmbuf_alloc_bulk (" << strerror(abs(retval)) << "); returning from thread...";
    return 2;
  }

  if (info.is_receiver) {

    uint16_t nb_rx = 0;
    int bytes_in_burst = 0;
    int bad_packets_in_burst = 0;
    
    // Here, I'm taking advantage of the fact that the transmitting
    // thread calls construct_packets_for_burst, which itself fills
    // the DAQEthHeader structure inside the packet it sends using
    // "set_daqethheader_test_values". I'll compare the received
    // packet's DAQEthHeader to this one and make sure they're equal
    
    DAQEthHeader daqethheader_reference;
    udp::set_daqethheader_test_values(daqethheader_reference);
    DAQEthHeader* daqethheader_ptr = nullptr;
    
    while (true) {

      bytes_in_burst = 0; // Quicker to add up the bytes in the packets in a local variables, then attach the total to the atomic variable used in stats reporting
      bad_packets_in_burst = 0; // Same thinking
      
      nb_rx = rte_eth_rx_burst(info.iface_id, 0, bufs, burst_size);
      num_received_packets += nb_rx;

      for (int i_p = 0; i_p < nb_rx; ++i_p) {
	bytes_in_burst += bufs[i_p]->pkt_len;

	// DAQEthHeader (should be) the first thing after the ethernet + IPv4 + UDP headers
	daqethheader_ptr = rte_pktmbuf_mtod_offset(bufs[0], DAQEthHeader*, sizeof(udp::ipv4_udp_packet_hdr));
	if (*daqethheader_ptr != daqethheader_reference) {
	  bad_packets_in_burst++;
	} 
      }

      num_received_bytes += bytes_in_burst;
      num_bad_packets += bad_packets_in_burst;

      // Free any unsent packets
      if (unlikely(nb_rx < burst_size)) {
      	rte_pktmbuf_free_bulk(bufs, burst_size - nb_rx);
      }
    }
    
  } else {

    rte_ether_addr dst_mac_addr_struct;
    
    // Notice that in the snippet of code below, the assumption is that
    // the first entry in the vector of thread configurations
    // corresponds to the receiving thread
    uint16_t receiver_iface = std::numeric_limits<uint16_t>::max();
    {
      std::lock_guard<std::mutex> guard(lcore_thread_mutex);
      receiver_iface = ptr_to_vector_of_thread_configs->at(0).iface_id;
    }

    retval = rte_eth_macaddr_get(receiver_iface, &dst_mac_addr_struct);
    
    if (retval == 0) {
      TLOG() << "Will send to ethernet interface with MAC address " << ealutils::get_mac_addr_str(dst_mac_addr_struct);
    } else {
      TLOG() << "Problem trying to obtain the MAC address of the destination port; returning...";
      return 3;
    }
  
    const std::string dst_mac_addr = ealutils::get_mac_addr_str(dst_mac_addr_struct); 
    udp::construct_packets_for_burst(info.iface_id, dst_mac_addr, payload_bytes, burst_size, bufs);

    TLOG() << "Dump of the first packet header: ";
    TLOG() << udp::get_udp_header_str(bufs[0]);

    TLOG() << "Dump of the first packet rte_mbuf object: ";
    TLOG() << udp::get_rte_mbuf_str(bufs[0]);
    
    rte_mbuf_sanity_check(bufs[0], 1);
    
    uint16_t nb_tx = 0;

    while (true) {

      int queue_id = 0; 
      //TLOG() << "Just about to call rte_eth_tx_burst with arguments info.iface_id == " << info.iface_id << ", second argument " << queue_id << ", bufs at " << bufs << " and burst size " << burst_size;
      nb_tx = rte_eth_tx_burst(info.iface_id, queue_id, bufs, burst_size);
      num_sent_packets += nb_tx;
      num_sent_bytes += nb_tx * bufs[0]->pkt_len;
      num_sent_bursts++;
      //TLOG() << "Sent " << nb_tx;
      
      // Free any unsent packets
      if (unlikely(nb_tx < burst_size)) {
      	rte_pktmbuf_free_bulk(bufs, burst_size - nb_tx);
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {

  CLI::App app{"test transmit and receive"};

  std::stringstream payload_desc;
  payload_desc << "Bytes of payload past the DAQEthHeader header (default " << payload_bytes << " bytes)";
  app.add_option("--payload", payload_bytes, payload_desc.str());

  CLI11_PARSE(app, argc, argv);
    
  argc = 1; // So rte_eal_init doesn't interpret what gets passed to the command line

  int retval = rte_eal_init(argc, argv);

  if (retval < 0) {
    rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed, info: %s\n", strerror(abs(retval)));
  }

  int nb_ports = rte_eth_dev_count_avail();
  TLOG() << "There are " << nb_ports << " ethernet ports available out of a total of " << rte_eth_dev_count_total();
  
  if (nb_ports < 2) {
    rte_exit(EXIT_FAILURE, "ERROR: fewer than 2 ethernet ports are available.\nYou need one port for transmitting packets and one port for receiving them\n");
  }

  auto stats = std::thread([&]() {
			     long sleep_time = 1;
			     while (true) {
			       TLOG() << "\nRates: \n" << num_sent_bursts / static_cast<double>(sleep_time) << " sent bursts/s\n" << num_sent_packets / static_cast<double>(sleep_time) << " sent packets/s\n" << num_received_packets / static_cast<double>(sleep_time) << " received packets/s\n" << num_bad_packets / static_cast<double>(sleep_time) << " bad packets/s\n"<< num_sent_bytes / static_cast<double>(sleep_time) << " sent bytes/s\n" << num_received_bytes / static_cast<double>(sleep_time) << " received bytes/s\n";
			       num_sent_packets.exchange(0);
			       num_sent_bytes.exchange(0);
			       num_sent_bursts.exchange(0);
			       num_received_packets.exchange(0);
			       num_received_bytes.exchange(0);
			       num_bad_packets.exchange(0);
			       std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
			     }
			   });


  uint16_t portid = std::numeric_limits<uint16_t>::max();
  int n_rx_qs = 0;
  int n_tx_qs = 0;
  uint16_t rx_ring_size = 1024;
  uint16_t tx_ring_size = 1024;
  int port_cntr = -1;
  std::map<int, std::unique_ptr<rte_mempool>> mbuf_pools;
  std::vector<lcore_thread_arg> lcore_thread_args;
  
  RTE_ETH_FOREACH_DEV(portid) { // RTE_ETH_FOREACH_DEV since no guarantee dpdk counts port IDs as 0, 1, 2 ...

    port_cntr++; // Initialize at -1, start at 0

    // Right now (May-2-2023) iface_init's implementation explicitly
    // assumes that the pools corresponding to <N> receiver queues
    // correspond to keys 0...N-1 in a map whose values are
    // unique_ptrs to pools
    
    std::stringstream poolname;
    poolname << "MBP-" << portid;
    mbuf_pools[port_cntr] = ealutils::get_mempool(poolname.str());

    bool is_receiver = false;
    
    if (port_cntr == 0) {
      n_tx_qs = 0;
      n_rx_qs = 1;
      is_receiver = true;
    } else if (port_cntr == 1) {
      n_tx_qs = 1;
      n_rx_qs = 0;
      is_receiver = false;
    }
    
    retval = ealutils::iface_init(portid, n_rx_qs, n_tx_qs, rx_ring_size, tx_ring_size, mbuf_pools);
    
    if (retval != 0) {
      rte_eal_cleanup();
      
      std::stringstream errstr;
      errstr << "A failure occurred initializing ethernet interface #" << portid << " (" << strerror(abs(retval)) << "); exiting...";
      rte_exit(EXIT_FAILURE, errstr.str().c_str());
    }

    auto pool = mbuf_pools[port_cntr].release();
    mbuf_pools.erase( mbuf_pools.find(port_cntr) );
    
    lcore_thread_args.emplace_back( lcore_thread_arg { pool, portid, is_receiver } );
  }

  rte_eal_mp_remote_launch(lcore_main, &lcore_thread_args, SKIP_MAIN);

  rte_eal_mp_wait_lcore();
  rte_eal_cleanup();
  
  int mynum = 55;
  TLOG() << "Things are still OK at this line, and here's a number: " << mynum;

  return 0;
}
