/**
 * @file test_stats_reporting.cxx Basic checking that stats reporting works as expected
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
  
}
  
using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

int main(int argc, char* argv[]) {

  CLI::App app{"test stats reporting"};

  const std::string default_mac_address = "6c:fe:54:47:98:20";
  
  app.add_option("--dst-mac", dst_mac_addr, "Destination MAC address (default " + default_mac_address + ")");

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
  
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL")).c_str(), NUM_MBUFS * rte_eth_dev_count_avail(),
							  MBUF_CACHE_SIZE, 0, buffer_size, rte_socket_id());


  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_pool_create failed, info: %s\n", rte_strerror(rte_errno));
  }
   
  struct rte_mbuf** bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  if (bufs == NULL) {
    TLOG(TLVL_ERROR) << "Failure trying to acquire memory for buffers; exiting...";
    std::exit(1);
  }

  retval = rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

  if (retval != 0) {
    rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_alloc_bulk failed, info: %s\n", strerror(abs(retval)));
  }

  int iface = 0;
  construct_packets_for_burst(iface, dst_mac_addr, payload_bytes, burst_size, bufs);

  rte_mbuf_sanity_check(bufs[0], 1);

  char* udp_payload = udp::get_udp_payload(bufs[0]);
  auto daq_hdr = reinterpret_cast<detdataformats::DAQEthHeader*>(udp_payload);
  
  PacketInfoAccumulator processor;
  processor.process_packet(*daq_hdr, bufs[0]->data_len);
  processor.process_packet(*daq_hdr, bufs[0]->data_len);

  TLOG() << "Dump after processing two identical packets while ignoring size, timestamp and sequence ID: ";
  processor.dump();

  // Expected step in sequence ID and timestamp is zero, expected size
  // is bufs[0]->data_len

  PacketInfoAccumulator processor2(0, bufs[0]->data_len);
  TLOG() << "Dump after processing two identical packets while expecting no change in the sequence ID and timestamp, and setting a (correct) expectation about the packet sizes: ";
  processor2.process_packet(*daq_hdr, bufs[0]->data_len);
  processor2.process_packet(*daq_hdr, bufs[0]->data_len);
  processor2.dump();

  // Expected step in sequence ID and timestamp is 1, expected size in
  // 999 bytes. All three expectations will fail with our identical,
  // constructed packets.

  TLOG() << "Dump after processing two identical packets while incorrectly expecting the sequence ID and timestamp to increment by 1, and incorrectly expecting the packet size to be 999 bytes: ";
  
  PacketInfoAccumulator processor3(1, 999);
  processor3.process_packet(*daq_hdr, bufs[0]->data_len);
  processor3.process_packet(*daq_hdr, bufs[0]->data_len);
  processor3.dump();  

  // Make the constructed packet appear as if it's from a different stream

  daq_hdr->stream_id++ ; 

  processor3.process_packet(*daq_hdr, bufs[0]->data_len);
  processor3.process_packet(*daq_hdr, bufs[0]->data_len);

  TLOG() << "Dump after adding two more packets, where I've changed the stream. Should get reports for two different streams now: ";
  processor3.dump();
    
  rte_eal_cleanup();

  return 0;
}
