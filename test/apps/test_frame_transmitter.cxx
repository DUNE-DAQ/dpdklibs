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

void lcore_main(uint16_t iface) {


    uint16_t lid = rte_lcore_id();

    if (lid != 1) {
      return;
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
    
    struct ipv4_udp_packet_hdr packet_hdr;

  constexpr int eth_header_bytes = 14;
  constexpr int udp_header_bytes = 8;
  constexpr int ipv4_header_bytes = 20;

  int eth_packet_bytes = eth_header_bytes + ipv4_header_bytes + udp_header_bytes + sizeof(detdataformats::DAQEthHeader) + payload_bytes; 
  int ipv4_packet_bytes = eth_packet_bytes - eth_header_bytes;
  int udp_datagram_bytes = ipv4_packet_bytes - ipv4_header_bytes;

  // Get info for the ethernet header (protocol stack level 2)
  pktgen_ether_hdr_ctor(&packet_hdr, dst_mac_addr);
  
  // Get info for the internet header (protocol stack level 3)
  pktgen_ipv4_ctor(&packet_hdr, ipv4_packet_bytes);

  // Get info for the UDP header (protocol stack level 4)
  pktgen_udp_hdr_ctor(&packet_hdr, udp_datagram_bytes);

  detdataformats::DAQEthHeader daqethheader_obj;
  set_daqethheader_test_values(daqethheader_obj);

  void* dataloc = nullptr;
  for (int i_pkt = 0; i_pkt < burst_size; ++i_pkt) {

    dataloc = rte_pktmbuf_mtod(bufs[i_pkt], char*);
    rte_memcpy(dataloc, &packet_hdr, sizeof(packet_hdr));

    dataloc = rte_pktmbuf_mtod_offset(bufs[i_pkt], char*, sizeof(packet_hdr));
    rte_memcpy(dataloc, &daqethheader_obj, sizeof(daqethheader_obj));

    bufs[i_pkt]->pkt_len = eth_packet_bytes;
    bufs[i_pkt]->data_len = eth_packet_bytes;
  }

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
      num_bytes += nb_tx * eth_packet_bytes; // n.b. Assumption in this line is that each packet is the same size
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

    uint16_t portid = 0;
    std::map<int, std::unique_ptr<rte_mempool>> dummyarg;
    retval = ealutils::iface_init(portid, n_rx_qs, n_tx_qs, dummyarg);

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
    
    TLOG() << "Name of the interface is " << dev_info.device->name;

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
    
    rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, NULL, SKIP_MAIN);

    rte_eal_mp_wait_lcore();
    rte_eal_cleanup();

    return 0;
}
