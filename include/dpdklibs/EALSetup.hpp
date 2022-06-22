/**
 * @file EALSetup.hpp EAL setup functions for DPDK
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_

#include <rte_eal.h>
#include <rte_ethdev.h>

namespace dunedaq {
namespace dpdklibs {

#define RX_RING_SIZE 512
#define TX_RING_SIZE 512

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
      //.max_rx_pkt_len = 9000,
        //.offloads = DEV_RX_OFFLOAD_JUMBO_FRAME,
    },
    .txmode = {
        .offloads = (DEV_TX_OFFLOAD_IPV4_CKSUM |
                     DEV_TX_OFFLOAD_UDP_CKSUM),
},
};

static inline int
port_init(uint16_t port, struct rte_mempool* mbuf_pool)
{
  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = 1, tx_rings = 3;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf txconf;

  if (!rte_eth_dev_is_valid_port(port))
    return -1;
  
  rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
  { /* scope */
    uint16_t mtu;
    rte_eth_dev_get_mtu(port, &mtu);
    printf(" port: %i mtu = %i\n", port, mtu);
  } 

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

rte_mempool* setup_eal(int argc, char* argv[]) {

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
  if (nb_ports < 2 || (nb_ports & 1)) {
      rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
  }

  printf("RTE_MBUF_DEFAULT_BUF_SIZE = %d\n", RTE_MBUF_DEFAULT_BUF_SIZE);

  mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
      MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (mbuf_pool == NULL) {
      rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
  }

  // Initialize all ports
  RTE_ETH_FOREACH_DEV(portid) {
      if (port_init(portid, mbuf_pool) != 0) {
          rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
      }
  }
  return mbuf_pool;

}

void finish_eal() {
  rte_eal_cleanup();
}


} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_