
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/udp/Utils.hpp"

#include "logging/Logging.hpp"

#include "rte_cycles.h"
#include "rte_eal.h"
#include "rte_ethdev.h"
#include "rte_lcore.h"
#include "rte_mbuf.h"

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"


#include <inttypes.h>
#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

namespace {

  // Only 8 and above works
  constexpr int burst_size = 256;

  constexpr int buffer_size = 9800; // Same number as in EALSetup.hpp
  
  std::string dst_ip_addr = "127.0.0.0";
  std::string src_ip_addr = "127.0.0.0";

  std::string dst_mac_addr = "";
  std::string src_mac_addr = "";

  int src_port = -1;
  int dst_port = -1;
  
  int payload_bytes = 0; // payload past the UDP header
}
  
using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

void print_rte_mbuf(const rte_mbuf& mbuf) {
  std::stringstream ss;

  ss << "\nrte_mbuf info:";
  ss << "\npkt_len: " << mbuf.pkt_len;
  ss << "\ndata_len: " << mbuf.data_len;
  ss << "\nBuffer address: " << std::hex << mbuf.buf_addr;
  ss << "\nRef count: " << std::dec << rte_mbuf_refcnt_read(&mbuf);
  ss << "\nport: " << mbuf.port;
  ss << "\nol_flags: " << std::hex << mbuf.ol_flags;
  ss << "\npacket_type: " << std::dec << mbuf.packet_type;
  ss << "\nl2 type: " << static_cast<int>(mbuf.l2_type);
  ss << "\nl3 type: " << static_cast<int>(mbuf.l3_type);
  ss << "\nl4 type: " << static_cast<int>(mbuf.l4_type);
  ss << "\ntunnel type: " << static_cast<int>(mbuf.tun_type);
  ss << "\nInner l2 type: " << static_cast<int>(mbuf.inner_l2_type);
  ss << "\nInner l3 type: " << static_cast<int>(mbuf.inner_l3_type);
  ss << "\nInner l4 type: " << static_cast<int>(mbuf.inner_l4_type);
  ss << "\nbuf_len: " << mbuf.buf_len;
  ss << "\nl2_len: " << mbuf.l2_len;
  ss << "\nl3_len: " << mbuf.l3_len;
  ss << "\nl4_len: " << mbuf.l4_len;
  ss << "\nouter_l2_len: " << mbuf.outer_l2_len;
  ss << "\nouter_l3_len: " << mbuf.outer_l3_len;
  
  TLOG() << ss.str().c_str();
}


static const struct rte_eth_conf iface_conf_default = {
    .txmode = {
        .offloads = (DEV_TX_OFFLOAD_IPV4_CKSUM |
                     DEV_TX_OFFLOAD_UDP_CKSUM),
     }
};

void lcore_main(uint16_t iface) {


    uint16_t lid = rte_lcore_id();

    if (lid > 2) return;

    TLOG () << "Going to sleep with lid = " << lid;
    rte_delay_us_sleep((lid + 1) * 1000021);

    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL") + std::to_string(lid)).c_str(), NUM_MBUFS * rte_eth_dev_count_avail(),
        MBUF_CACHE_SIZE, 0, buffer_size, rte_socket_id());

    if (mbuf_pool == NULL) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_pool_create failed, info: %s\n", rte_strerror(rte_errno));
    }
    
    TLOG() << "\n\nCore " << rte_lcore_id() << " transmitting packets. [Ctrl+C to quit]\n\n";


    int burst_number = 0;
    std::atomic<int> num_frames = 0;

    // auto stats = std::thread([&]() {
    //   while (true) {
    //     TLOG() << "[DO NOT TRUST] Rate is " << sizeof(struct ipv4_udp_packet) * num_frames / 1e6 * 8;
    //     num_frames.exchange(0);
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    //   }
    // });

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

  // JCF, Apr-3-2023: enp225s0f1 is the NIC we want on np02-srv-001
  const std::string enp225s0f1_macaddr = "d8:5e:d3:8c:c4:e3";
  const std::string enp225s0f0_ipv4addr = "192.168.2.1";
    
  // JCF, Apr-3-2023: ens801f0np0 is the NIC we want on np04-srv-022
  const std::string ens801f0np0_macaddr = "ec:0d:9a:8e:b9:88";
  const std::string ens801f0np0_ipv4addr = "10.73.139.17";

  // JCF, Apr-4-2023: 6c:fe:54:47:a1:28 is what dpdk appears to think is the default source NIC on np04-srv-001
  const std::string important_macaddr = "6c:fe:54:47:a1:28";
  
  constexpr int udp_header_bytes = 8;
  constexpr int ipv4_header_bytes = 20;
  int ipv4_packet_bytes = ipv4_header_bytes + udp_header_bytes + payload_bytes; 
  int udp_datagram_bytes = udp_header_bytes + payload_bytes; 

  if (dst_mac_addr == "") {
    dst_mac_addr = enp225s0f1_macaddr;
  }

  TLOG() << "Input destination IP address: " << dst_ip_addr;
  TLOG() << "Input destination MAC address: " << dst_mac_addr;
  TLOG() << "Input source IP address: " << src_ip_addr;
  TLOG() << "Input source MAC address: " << src_mac_addr;
  
  // Get info for the ethernet header (procol stack level 3)
  pktgen_ether_hdr_ctor(&packet_hdr, dst_mac_addr);
  if (src_mac_addr == "00:00:00:00:00:00") {
    get_ether_addr6(dst_mac_addr.c_str(), &packet_hdr.eth_hdr.src_addr); // Otherwise this gets derived from the port inside pktgen_ether_hdr_ctor
  }
  
  // Get info for the internet header (procol stack level 3)
  pktgen_ipv4_ctor(&packet_hdr, ipv4_packet_bytes, src_ip_addr, dst_ip_addr);

  // Get info for the UDP header (procol stack level 4)
  if (src_port != -1 && dst_port != -1) {
    pktgen_udp_hdr_ctor(&packet_hdr, udp_datagram_bytes, src_port, dst_port);
  } else if (src_port != -1 && dst_port == -1) {
    pktgen_udp_hdr_ctor(&packet_hdr, udp_datagram_bytes, src_port);
  } else if (src_port == -1 && dst_port == -1) {
    pktgen_udp_hdr_ctor(&packet_hdr, udp_datagram_bytes);
  } else {
    TLOG(TLVL_ERROR) << "Illegal requested port combination (source port == " << src_port << ", destination port == " << dst_port << "). Exiting...";
    rte_eal_cleanup();
    std::exit(3);
  }

  for (int i_pkt = 0; i_pkt < burst_size; ++i_pkt) {

    void* datastart = rte_pktmbuf_mtod(bufs[i_pkt], char*);
    rte_memcpy(datastart, &packet_hdr, sizeof(packet_hdr));
    
    bufs[i_pkt]->l2_len = sizeof(struct rte_ether_hdr);
    bufs[i_pkt]->l3_len = sizeof(struct rte_ipv4_hdr);
    bufs[i_pkt]->l4_len = sizeof(struct rte_udp_hdr);
    
    bufs[i_pkt]->pkt_len = ipv4_packet_bytes;
    bufs[i_pkt]->data_len = ipv4_packet_bytes;
  }

  TLOG() << "JCF: dump of the first mbuf: ";
  print_rte_mbuf(*bufs[0]);
  
  TLOG() << "JCF: Dump of the first packet header: ";
  TLOG() << get_udp_header_str(bufs[0]);
  
  
  TLOG() << "JCF: Dump of the first packet:";
  rte_pktmbuf_dump(stdout, bufs[0], 100);
  
  rte_mbuf_sanity_check(bufs[0], 1);

  TLOG() << "pkt_len of the first packet: " << rte_pktmbuf_pkt_len(bufs[0]);
  TLOG() << "data_len of the first packet: " << rte_pktmbuf_data_len(bufs[0]);
  
  int cntr = 0;
  //  while (cntr++ < std::numeric_limits<int>::max()) {
  //  while (false) {
  while (cntr++ < 3) {
  
      burst_number++;
      if (lid != 1) {
	TLOG() << "Skipping burst on thread #" << lid << " as we're only bursting on thread 1";
	break;
      }

      if (burst_number % 250000 == 0) {
	TLOG() << "Thread #" << lid << ", burst #" << burst_number;
      }
      
      int sent = 0;
      uint16_t nb_tx = 0;
      //      while(sent < burst_size)
      //	{
	  nb_tx = rte_eth_tx_burst(iface, lid-1, bufs, burst_size - sent);
	  sent += nb_tx;
	  num_frames += nb_tx;
	  //	  TLOG() << "num_frames == " << num_frames;
	  //	}

      /* Free any unsent packets. */
      if (unlikely(nb_tx < burst_size))
      {
	TLOG(TLVL_WARNING) << "Only " << nb_tx << " frames were sent on thread #" << lid << ", less than burst size of " << burst_size;
	rte_pktmbuf_free_bulk(bufs, burst_size - nb_tx);
      }
      // retval = rte_eth_tx_done_cleanup(iface, lid-1, 0);
      // if (retval != 0) {
      // 	rte_exit(EXIT_FAILURE, "ERROR: failure calling rte_eth_tx_done_cleanup, info: %s\n", strerror(abs(retval)));
      // }
  }
}

int main(int argc, char* argv[]) {

  CLI::App app{"test frame transmitter"};
    app.add_option("--dst-ip", dst_ip_addr, "Destination IP address");
    app.add_option("--src-ip", src_ip_addr, "Source IP address");

    app.add_option("--dst-mac", dst_mac_addr, "Destination MAC address");
    app.add_option("--src-mac", src_mac_addr, "Source MAC address");

    app.add_option("--src-port", src_port, "Source UDP port");
    app.add_option("--dst-port", dst_port, "Destination UDP port");
    
    app.add_option("--payload", payload_bytes, "Bytes of payload past the UDP header");
    
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
    if (nb_ports & 1) {
        rte_exit(EXIT_FAILURE, "ERROR: number of available ethernet ports must be even\n");
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

    // std::map<int, std::unique_ptr<rte_mempool>> mbuf_pools;
    
    // for (size_t i_q = 0; i_q < n_tx_qs; ++i_q) {
    //   std::stringstream ss;
    //   ss << "MBP-" << i_q;
    //   mbuf_pools[i_q] = ealutils::get_mempool(ss.str());
    // }

    rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, NULL, SKIP_MAIN);

    rte_eal_mp_wait_lcore();
    rte_eal_cleanup();

    return 0;
}
