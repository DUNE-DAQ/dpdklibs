/**
 * @file test_frame_transmitter.cxx Construct UDP packets to be sent over the wire using DPDK
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

#include "rte_cycles.h"
#include "rte_dev.h"
#include "rte_eal.h"
#include "rte_ethdev.h"
#include "rte_lcore.h"
#include "rte_mbuf.h"

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include <iostream>
#include <chrono>
#include <thread>


namespace {

  // Only 8 and above works
  constexpr int burst_size = 256;

  constexpr int buffer_size = 9800; // Same number as in EALSetup.hpp

  std::string dst_mac_addr = "";
  int payload_bytes = 0; // payload past the DAQEthHeader

  std::atomic<long> num_bursts = 0;
  std::atomic<long> num_packets = 0;
  std::atomic<long> num_bytes = 0;
}
  
using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

void lcore_main(void* arg) {

  uint16_t iface = *( static_cast<uint16_t*>(arg) );

  uint16_t lid = rte_lcore_id();

  if (lid != 1) {
    return;
  }

  if (rte_eth_dev_socket_id(iface) >= 0 && rte_eth_dev_socket_id(iface) != static_cast<int>(rte_socket_id())) {
    TLOG(TLVL_WARNING) << "WARNING, dpdk interface " << iface << " is on remote NUMA node to polling thread.\nrte_eth_dev_socket_id(" << iface << ") == " << rte_eth_dev_socket_id(iface) << ", rte_socket_id() == " << static_cast<int>(rte_socket_id()) << "\nPerformance will not be optimal.\n";
  }
  
  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL") + std::to_string(lid)).c_str(), NUM_MBUFS * rte_eth_dev_count_avail(),
							  MBUF_CACHE_SIZE, 0, buffer_size, rte_socket_id());


  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_pool_create failed, info: %s\n", rte_strerror(rte_errno));
  }
   
  struct rte_mbuf** bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  if (bufs == NULL) {
    TLOG(TLVL_ERROR) << "Failure trying to acquire memory for transmission buffers on thread #" << lid << "; exiting...";
    std::exit(1);
  }

  int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

  if (retval != 0) {
    rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_alloc_bulk failed, info: %s\n", strerror(abs(retval)));
  }
  
  TLOG() << "In thread, we have iface == " << iface;
  construct_packets_for_burst(iface, dst_mac_addr, payload_bytes, burst_size, bufs);

  TLOG() << "Dump of the first packet header: ";
  TLOG() << get_udp_header_str(bufs[0]);

  TLOG() << "Dump of the first packet rte_mbuf object: ";
  TLOG() << get_rte_mbuf_str(bufs[0]);

  rte_mbuf_sanity_check(bufs[0], 1);
  
  TLOG() << "\n\nCore " << rte_lcore_id() << " transmitting packets. [Ctrl+C to quit]\n\n";
  
  uint16_t nb_tx = 0;
  int cntr = 0;
  while (cntr < std::numeric_limits<int>::max()) {
    cntr++;

      nb_tx = rte_eth_tx_burst(iface, lid-1, bufs, burst_size);
      num_packets += nb_tx;
      num_bytes += nb_tx * bufs[0]->pkt_len; // n.b. Assumption in this line is that each packet is the same size
      num_bursts++;
      
      // Free any unsent packets
      if (unlikely(nb_tx < burst_size)) {
	rte_pktmbuf_free_bulk(bufs, burst_size - nb_tx);
      }
  }
}

int main(int argc, char* argv[]) {

  CLI::App app{"test frame transmitter"};

  const std::string default_mac_address = "6c:fe:54:47:98:20";
  
  app.add_option("--dst-mac", dst_mac_addr, "Destination MAC address (default " + default_mac_address + ")");
  app.add_option("--payload", payload_bytes, "Bytes of payload past the DAQEthHeader header");

  if (dst_mac_addr == "") {
    dst_mac_addr = default_mac_address;
  }
    
    CLI11_PARSE(app, argc, argv);
    
    argc = 1; // Set to 1 so rte_eal_init ignores all the CLI-parsed arguments

    int retval = rte_eal_init(argc, argv);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed, info: %s\n", strerror(abs(retval)));
    }

    // Check that there is an even number of ports to send/receive on
    auto nb_ports = rte_eth_dev_count_avail();
    TLOG() << "There are " << nb_ports << " ethernet ports available out of a total of " << rte_eth_dev_count_total();
    if (nb_ports == 0) {
      rte_exit(EXIT_FAILURE, "ERROR: 0 ethernet ports are available. This can be caused either by someone else currently\nusing dpdk-based code or the necessary drivers not being bound to the NICs\n(see https://github.com/DUNE-DAQ/dpdklibs#readme for more)\n");
    }
    
    const uint16_t n_tx_qs = 1;
    const uint16_t n_rx_qs = 0;
    const uint16_t rx_ring_size = 1024;
    const uint16_t tx_ring_size = 1024;

    uint16_t portid = 0;
    std::map<int, std::unique_ptr<rte_mempool>> dummyarg;
    retval = ealutils::iface_init(portid, n_rx_qs, n_tx_qs, rx_ring_size, tx_ring_size, dummyarg);

    if (retval != 0) {
      TLOG(TLVL_ERROR) << "A failure occurred initializing ethernet interface #" << portid << "; exiting...";
      rte_eal_cleanup();
      std::exit(2);
    }

    struct rte_eth_dev_info dev_info;
    retval = rte_eth_dev_info_get(portid, &dev_info);

    if (retval != 0) {
      TLOG(TLVL_ERROR) << "A failure occured trying to get info on ethernet interface #" << portid << "; exiting...";
      rte_eal_cleanup();
      std::exit(4);
    }
    
    TLOG() << "Name of the interface is " << rte_dev_name(dev_info.device);

    auto stats = std::thread([&]() {
    			       long sleep_time = 1;
			       while (true) {
				 TLOG() << "Rates: " << num_packets / static_cast<double>(sleep_time) << " packets/s, " << num_bytes / static_cast<double>(sleep_time) << " bytes/s, " << num_bursts / static_cast<double>(sleep_time) << " bursts/s" << "\n";
				 num_packets.exchange(0);
				 num_bytes.exchange(0);
				 num_bursts.exchange(0);
				 std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
			       }
			     });
    
    rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, &portid, SKIP_MAIN);

    rte_eal_mp_wait_lcore();
    rte_eal_cleanup();

    return 0;
}
