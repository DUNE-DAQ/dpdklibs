#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <iostream>
#include <iomanip>

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
bool jumbo_enabled = false;
bool is_debug = true;

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = 9000,
        .offloads = (DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM),
        }
};

static inline int
port_init(uint16_t port, struct rte_mempool* mbuf_pool)
{
  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = 1, tx_rings = 0;
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

    rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
  { /* scope */
    uint16_t mtu;
    rte_eth_dev_get_mtu(port, &mtu);
    TLOG() << "Port: " << port << " MTU: " << mtu;
  }

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0)
    return retval;

  /* Allocate and set up 1 RX queue per Ethernet port. */
  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
      return retval;
  }

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

  /* Enable RX in promiscuous mode for the Ethernet device. */
  retval = rte_eth_promiscuous_enable(port);
  if (retval != 0)
    return retval;

  return 0;
}

static int
lcore_main(struct rte_mempool *mbuf_pool)
{
  // int* is_running = (int*)arg;
  uint16_t port;

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  RTE_ETH_FOREACH_DEV(port)
  if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id())
    printf("WARNING, port %u is on remote NUMA node to "
           "polling thread.\n\tPerformance will "
           "not be optimal.\n",
           port);



  /* Run until the application is quit or killed. */
  int burst_number = 0;
  int sum = 0;
  std::atomic<int> num_frames = 0;

  auto stats = std::thread([&]() {
    while (true) {
      // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
      // TLOG() << "Rate is " << sizeof(struct ipv4_udp_packet) * num_frames / 1e6 * 8;
      TLOG() << "Rate is " << (size_t)9000 * num_frames / 1e6 * 8;
      // printf("Rate is %f\n", (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8);
      num_frames.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);
  bool once = true;
  while (true) {
    // printf("hello\n");
    RTE_ETH_FOREACH_DEV(port)
    {

      /* Get burst of RX packets, from first port of pair. */
      // struct rte_mbuf* bufs[burst_size];
      const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, burst_size);

      if (nb_rx != 0) {
        // TLOG() << "nb_rx = " << nb_rx;
        // TLOG() << "bufs.buf_len = " << bufs[0]->data_len;

        // Doesn't correspond to the packets we are expecting to receive
        if (bufs[0]->data_len != sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) {
          std::stringstream ss;
          for (int i = 0; i < nb_rx; i++) {
            ss << bufs[i]->pkt_len << " ";
            // TLOG() << "Found other data" << ss.str();
            if (false) {
            rte_pktmbuf_dump(stdout, bufs[i], bufs[i]->pkt_len);
            once = false;
            }
          }
          // continue;
        }

        // if (burst_number % 1000 == 0) {
        //   TLOG() << "burst_number =" << burst_number;
        // }
        for (int i=0; i<nb_rx; ++i) {
          num_frames++;
          // auto fr = rte_pktmbuf_mtod_offset(bufs[i], detdataformats::wib::WIBFrame*, sizeof(struct rte_ether_hdr));
          // if (fr->get_timestamp() != burst_number) {
          //   TLOG() << "Packets are lost";
          //   burst_number = fr->get_timestamp();
          //   sum = fr->get_channel(190);
          // }
          // else {
          //   sum += fr->get_channel(190);
          //   if (sum == 28) {
          //     // TLOG() << "All frames received for burst number " << burst_number;
          //     burst_number++;
          //     sum = 0;
          //   }
          // }
        }

        for (int i=0; i < nb_rx; i++)
        {
          rte_pktmbuf_free(bufs[i]);
        }
      }
    }
  }
  return 0;
}

int
main(int argc, char* argv[])
{
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    // Init EAL
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
    }

    argc -= ret;
    argv += ret;

    // Check that there is an even number of ports to send/receive on
    nb_ports = rte_eth_dev_count_avail();
    printf("Available ports: %d\n", nb_ports);
    //if (nb_ports < 2 || (nb_ports & 1)) {
    //    rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
    //}

    printf("RTE_MBUF_DEFAULT_BUF_SIZE = %d\n", RTE_MBUF_DEFAULT_BUF_SIZE);

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
        // MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        MBUF_CACHE_SIZE, 0, 9800, rte_socket_id());

    if (mbuf_pool == NULL) {
        rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    }

    // Initialize all ports
    RTE_ETH_FOREACH_DEV(portid) {
        if (port_init(portid, mbuf_pool) != 0) {
            rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
        }
    }

    // Call lcore_main on the main core only
    // for (int i=0; i < 2; ++i) {
    //   rte_eal_remote_launch(lcore_main, mbuf_pool, i);
    // }
    lcore_main(mbuf_pool);

    // clean up the EAL
    rte_eal_cleanup();

    return 0;
}
