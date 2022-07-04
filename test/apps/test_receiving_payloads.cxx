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

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 8



using namespace dunedaq;

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = 9000,
        },
};

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
  TLOG() << "Calling lcore";
  int* is_running = (int*)arg;
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
  while (*is_running != 0) {
    /*
     * Receive packets on a port and forward them on the paired
     * port. The mapping is 0 -> 1, 1 -> 0, 2 -> 3, 3 -> 2, etc.
     */
    RTE_ETH_FOREACH_DEV(port)
    {

      /* Get burst of RX packets, from first port of pair. */
      struct rte_mbuf* bufs[BURST_SIZE];
      const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

      if (nb_rx != 0) {
        TLOG() << "nb_rx = " << nb_rx;
        // printf("nb_rx = %d\n", nb_rx);
        TLOG() << "bufs.buf_len = " << bufs[0]->data_len;
        std::ostringstream ss;
        // TLOG() << reinterpret_cast<char*>(bufs);
        // auto fr = reinterpret_cast<detdataformats::wib::WIBFrame*>(bufs[0]->buf_addr + 42);
        // auto fr = rte_pktmbuf_mtod(bufs[0], char*);
        // auto fr = static_cast<char*>(bufs[0]->data);
        // for (int i = 0; i < 256; ++i) {
        //   ss << std::to_string(fr->get_channel(i)) << " ";
        // }

        // TLOG () << fr;
        // for(int i=0; i<(*bufs)->data_len / sizeof(char); ++i) {
        //   TLOG() << reinterpret_cast<char*>(bufs)[i]) << " ";
        // }
        // TLOG() << ss;
        // rte_pktmbuf_dump(stdout, bufs[0], bufs[0]->data_len);

        // for (int i = 0; i < nb_rx; ++i) {
        //     rte_pktmbuf_dump(stdout, bufs[i], bufs[i]->pkt_len);
        // }
        // stringstream ss;
        for (int i=0; i<500; ++i) {
          std::cout
                    << std::setw(2) << std::hex << rte_pktmbuf_mtod(bufs[0], char*)[i] << " ";
        }
        // TLOG() << ss.str();
      }

    }
  }

  return 0;
}

int
main(int argc, char* argv[])
{
  struct rte_mempool* mbuf_pool;
  unsigned nb_ports;
  uint16_t portid;
  int run_flag = 0;

  /* Initialize the Environment Abstraction Layer (EAL). */
  int ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

  argc -= ret;
  argv += ret;

  /* Check that there is an even number of ports to send/receive on. */
  nb_ports = rte_eth_dev_count_avail();
  TLOG() << "nb_ports: " << nb_ports;
  if (nb_ports < 2 || (nb_ports & 1))
    rte_exit(EXIT_FAILURE, "Error: number of ports must be even\n");

  /* Creates a new mempool in memory to hold the mbufs. */
  mbuf_pool = rte_pktmbuf_pool_create(
    "MBUF_POOL", NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

  /* Initialize all ports. */
  RTE_ETH_FOREACH_DEV(portid)
  if (port_init(portid, mbuf_pool) != 0)
    rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n", portid);

  if (rte_lcore_count() > 1)
    TLOG() << "WARNING: Too many lcores enabled. Only 1 used.";
  run_flag = 1;
  // rte_eal_remote_launch(lcore_main, &run_flag, 2);

  /* Call lcore_main on the main core only. */
  TLOG() << "Going to call lcore_main()";
  lcore_main(&run_flag);

  /* clean up the EAL */
  rte_eal_cleanup();

  return 0;
}
