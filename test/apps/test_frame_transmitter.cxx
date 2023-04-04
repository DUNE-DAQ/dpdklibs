#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

#include "logging/Logging.hpp"
#include "detdataformats/wib/WIBFrame.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif


// Apparently only 8 and above works
int burst_size = 256;
bool is_debug = false;

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

static const struct rte_eth_conf port_conf_default = {
    // .txmode = {
    //   .mtu = 9000,
    // },
    .txmode = {
        .offloads = (DEV_TX_OFFLOAD_IPV4_CKSUM |
                     DEV_TX_OFFLOAD_UDP_CKSUM),
},
};

static inline int
port_init(uint16_t port)
{
  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = 0, tx_rings = 2;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf txconf;

  if (!rte_eth_dev_is_valid_port(port))
    return -1;

  retval = rte_eth_dev_info_get(port, &dev_info);
  if (retval != 0) {
    printf("Error during getting device (port %u) info: %s\n", port, strerror(-retval));
    return retval;
  }

  if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
    port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0)
    return retval;

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0)
    return retval;

  txconf = dev_info.default_txconf;
  txconf.offloads = port_conf.txmode.offloads;
  /* Allocate and set up 1 TX queue per Ethernet port. */
  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0)
      return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if (retval < 0)
    return retval;

  /* Display the port MAC address. */
  struct rte_ether_addr addr;
  retval = rte_eth_macaddr_get(port, &addr);
  if (retval != 0)
    return retval;

  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
         port,
         addr.addr_bytes[0],
         addr.addr_bytes[1],
         addr.addr_bytes[2],
         addr.addr_bytes[3],
         addr.addr_bytes[4],
         addr.addr_bytes[5]);

  rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
  
  uint16_t mtu;
  rte_eth_dev_get_mtu(port, &mtu);
  TLOG() << "MTU = " << mtu;

  /* Enable RX in promiscuous mode for the Ethernet device. */
  // retval = rte_eth_promiscuous_enable(port);
  // if (retval != 0)
  //   return retval;

  return 0;
}

void lcore_main(void *arg) {


    uint16_t lid = rte_lcore_id();

    // if (pktgen_has_work())
    //     return 0;
    if (lid > 2) return;

    TLOG () << "Going to sleep with lid = " << lid;
    rte_delay_us_sleep((lid + 1) * 1000021);
    uint16_t port = std::numeric_limits<uint16_t>::max();

    unsigned nb_ports = rte_eth_dev_count_avail();
    TLOG () << "mbuf with lid = " << lid;
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL") + std::to_string(lid)).c_str(), NUM_MBUFS * nb_ports,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_pool_create failed, info: %s\n", rte_strerror(rte_errno));
    }
    TLOG () << "mbuf done with lid = " << lid;

    uint16_t portid;
    // Initialize all ports
    // RTE_ETH_FOREACH_DEV(portid) {
    // if (port_init(portid) != 0) {
    //     rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    // }
    // }

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  // RTE_ETH_FOREACH_DEV(port) {
  //     if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
  //         printf("WARNING, port %u is on remote NUMA node to "
  //                         "polling thread./n/tPerformance will "
  //                         "not be optimal.\n", port);
  //     }

  //     printf("INFO: Port %u has socket id: %u.\n", port, rte_eth_dev_socket_id(port));
  // }

    TLOG() << "\n\nCore " << rte_lcore_id() << " transmitting packets. [Ctrl+C to quit]\n\n";

  /* Run until the application is quit or killed. */
  int burst_number = 0;
  std::atomic<int> num_frames = 0;

  // auto stats = std::thread([&]() {
  //   while (true) {
  //     // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
  //     TLOG() << "[DO NOT TRUST] Rate is " << sizeof(struct ipv4_udp_packet) * num_frames / 1e6 * 8;
  //     num_frames.exchange(0);
  //     std::this_thread::sleep_for(std::chrono::seconds(1));
  //   }
  // });

  // std::this_thread::sleep_for(std::chrono::milliseconds(5));
  struct rte_mbuf **pkt = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  if (pkt == NULL) {
    TLOG(TLVL_ERROR) << "Call to malloc failed; exiting...";
    std::exit(1);
  }
  
  int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, pkt, burst_size);

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
  
  constexpr int payload_bytes = 0;
  constexpr int udp_header_bytes = 8;
  constexpr int ipv4_header_bytes = 20;
  constexpr int ipv4_packet_bytes = ipv4_header_bytes + udp_header_bytes + payload_bytes; 
  constexpr int udp_datagram_bytes = udp_header_bytes + payload_bytes; 
  
  // Get info for the ethernet header (procol stack level 3)
  pktgen_ether_hdr_ctor(&packet_hdr, enp225s0f1_macaddr);
  //pktgen_ether_hdr_ctor(&packet_hdr, important_macaddr);

  // Get info for the internet header (procol stack level 3)
  pktgen_ipv4_ctor(&packet_hdr, ipv4_packet_bytes, "127.0.0.0", "127.0.0.0");

  // Get info for the UDP header (procol stack level 4)
  pktgen_udp_hdr_ctor(&packet_hdr, udp_datagram_bytes);

  for (int i_pkt = 0; i_pkt < burst_size; ++i_pkt) {

    void* datastart = rte_pktmbuf_mtod(pkt[i_pkt], char*);
    rte_memcpy(datastart, &packet_hdr, sizeof(packet_hdr));

    pkt[i_pkt]->pkt_len = ipv4_packet_bytes;
    pkt[i_pkt]->data_len = ipv4_packet_bytes;
  }

  TLOG() << "JCF: Dump of the first packet header: ";
  TLOG() << packet_hdr;
  
  TLOG() << "JCF: Dump of the first packet:";
  rte_pktmbuf_dump(stdout, pkt[0], 100);
  
  rte_mbuf_sanity_check(pkt[0], 1);

  TLOG() << "pkt_len of the first packet: " << rte_pktmbuf_pkt_len(pkt[0]);
  TLOG() << "data_len of the first packet: " << rte_pktmbuf_data_len(pkt[0]);
  
  
  for (int i_set = 0; i_set < 1; ++i_set) {

    port = 0;

      burst_number++;

      /* Send burst of TX packets. */
      int sent = 0;
      uint16_t nb_tx;
      while(sent < burst_size)
      {
	nb_tx = rte_eth_tx_burst(port, lid-1, pkt, burst_size - sent);
        sent += nb_tx;
        num_frames += nb_tx;
	TLOG() << "num_frames == " << num_frames;
      }

      /* Free any unsent packets. */
      if (unlikely(nb_tx < burst_size))
      {
	TLOG(TLVL_WARNING) << "Only " << nb_tx << " frames were sent, less than burst size of " << burst_size;
        uint16_t buf;
        for (buf = nb_tx; buf < burst_size; buf++)
        {
          rte_pktmbuf_free(pkt[buf]);
        }
      }
      retval = rte_eth_tx_done_cleanup(port, lid-1, 0);
      if (retval != 0) {
	rte_exit(EXIT_FAILURE, "ERROR: failure calling rte_eth_tx_done_cleanup, info: %s\n", strerror(abs(retval)));
      }
  }
}

int main(int argc, char* argv[]) {
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    // Do *not* try to use argv after call to rte_eal_init; this function can modify the array
    int retval = rte_eal_init(argc, argv);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed, info: %s\n", strerror(abs(retval)));
    }

    // Check that there is an even number of ports to send/receive on
    nb_ports = rte_eth_dev_count_avail();
    TLOG() << "There are " << nb_ports << " ethernet ports available out of a total of " << rte_eth_dev_count_total();
    if (nb_ports == 0) {
      rte_exit(EXIT_FAILURE, "ERROR: 0 ethernet ports are available. This can be caused either by someone else currently\nusing dpdk-based code or the necessary drivers not being bound to the NICs\n(see https://github.com/DUNE-DAQ/dpdklibs#readme for more)\n");
    }
    if (nb_ports < 2 || (nb_ports & 1)) {
        rte_exit(EXIT_FAILURE, "ERROR: number of available ethernet ports must be even and greater than 0\n");
    }

    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 0, tx_rings = 2;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;
    uint16_t port = 0;

    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_configure failed, info: %s\n", strerror(abs(retval)));
    }

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_adjust_nb_rx_tx_desc failed, info: %s\n", strerror(abs(retval)));
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    txconf.tx_rs_thresh = 0;
    txconf.tx_thresh.wthresh = 0;
    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
      retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
      if (retval < 0) {
	rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_tx_queue_setup failed, info: %s\n", strerror(abs(retval)));
      }
    }

    retval = rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_set_mtu failed, info: %s\n", strerror(abs(retval)));
    }
    
    retval = rte_eth_dev_start(port);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_start failed, info: %s\n", strerror(abs(retval)));
    }

    /* Display the port MAC address. */
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_macaddr_get failed, info: %s\n", strerror(abs(retval)));
    }

    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
           port,
           addr.addr_bytes[0],
           addr.addr_bytes[1],
           addr.addr_bytes[2],
           addr.addr_bytes[3],
           addr.addr_bytes[4],
           addr.addr_bytes[5]);
    
    uint16_t mtu;
    rte_eth_dev_get_mtu(port, &mtu);
    TLOG() << "MTU = " << mtu;

    // mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
    //     MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    // if (mbuf_pool == NULL) {
    //     rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    // }

    // Initialize all ports
    // RTE_ETH_FOREACH_DEV(portid) {
    //     if (port_init(portid, mbuf_pool) != 0) {
    //         rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    //     }
    // }

    // Call lcore_main on the main core only
    // int res = rte_eal_remote_launch((lcore_function_t *) lcore_main, mbuf_pool, 1);

    // int res2 = rte_eal_remote_launch((lcore_function_t *) lcore_main, mbuf_pool, 2);
    rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, NULL, SKIP_MAIN);
    // TLOG() << "Result = " << res;
    // TLOG() << "Result = " << res;
    // lcore_main(mbuf_pool);

    // rte_eal_wait_lcore(1);
    // rte_eal_wait_lcore(2);
    // clean up the EAL
    // rte_eal_cleanup();
    rte_eal_mp_wait_lcore();

    return 0;
}
