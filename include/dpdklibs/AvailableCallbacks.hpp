/**
 * @file AvailableCallbacks.hpp Callback functions for DPDK tasks
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_AVAILABLECALLBACKS_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_AVAILABLECALLBACKS_HPP_

#include "DPDKDefinitions.hpp"

#include <rte_ethdev.h>

namespace dunedaq {
namespace dpdklibs {
namespace callbacks {

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
};

static int
lcore_hello(__attribute__((unused)) void *arg)
{
  unsigned lcore_id;
  lcore_id = rte_lcore_id();
  printf("hello from core %u\n", lcore_id);
  return 0;
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool* mbuf_pool)
{
  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = 1, tx_rings = 1;
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

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static int
lcore_main(void* arg)
{
  bool* is_running = (bool*)&arg;
  uint16_t port;

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  RTE_ETH_FOREACH_DEV(port)
  if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
    printf("WARNING, port %u is on remote NUMA node to "
           "polling thread.\n\tPerformance will "
           "not be optimal.\n",
           port);
  }

  printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n", rte_lcore_id());

  /* Run until the application is quit or killed. */
  while (*is_running) {
    /*
     * Receive packets on a port and forward them on the paired
     * port. The mapping is 0 -> 1, 1 -> 0, 2 -> 3, 3 -> 2, etc.
     */
    RTE_ETH_FOREACH_DEV(port)
    {

      /* Get burst of RX packets, from first port of pair. */
      struct rte_mbuf* bufs[BURST_SIZE];
      const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

      if (unlikely(nb_rx == 0))
        continue;

      printf("Packets received: %i\n", nb_rx);
      int buffer_len, number_segs, packet_len, dat_len, length_with_macro;
      for (int i = 0; i < nb_rx; ++i) {
        buffer_len = bufs[i]->buf_len;
        number_segs = bufs[i]->nb_segs;
        packet_len = bufs[i]->pkt_len;
        dat_len = bufs[i]->data_len;
        printf("Packet %i buf_len: %i, nb_segs: %i, pkt_len: %i, data_len: %i\n"
               , i, buffer_len, number_segs, packet_len, dat_len);
      }

//      /* Send burst of TX packets, to second port of pair. */
//      const uint16_t nb_tx = rte_eth_tx_burst(port ^ 1, 0, bufs, nb_rx);

//      /* Free any unsent packets. */
//      if (unlikely(nb_tx < nb_rx)) {
//        uint16_t buf;
//        for (buf = nb_tx; buf < nb_rx; buf++)
//          rte_pktmbuf_free(bufs[buf]);
//      }
    }
  }

  return 0;
}

} // namespace callbacks
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_AVAILABLECALLBACKS_HPP_