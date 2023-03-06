/**
 * @file EALSetup.hpp EAL setup functions for DPDK
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_

#include "logging/Logging.hpp"

#include <boost/program_options/parsers.hpp>

#include <rte_eal.h>
#include <rte_ethdev.h>

namespace dunedaq {
namespace dpdklibs {
namespace ealutils {

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

static volatile uint8_t dpdk_quit_signal; 

static const struct rte_eth_conf port_conf_default = {
  .rxmode = {
    //.mq_mode = ETH_MQ_RX_NONE, // Deprecated
    .mtu = 9000,
    .max_lro_pkt_size = 9000,
    .split_hdr_size = 0,
    //.offloads = DEV_RX_OFFLOAD_JUMBO_FRAME,
  },

  .txmode = {
    .offloads = (RTE_ETH_TX_OFFLOAD_MULTI_SEGS),
  },
};

static inline int
port_init(uint16_t port, uint16_t rx_rings, uint16_t tx_rings, 
	  std::map<int, std::unique_ptr<rte_mempool>>& mbuf_pool) //struct rte_mempool* mbuf_pool)
{
  struct rte_eth_conf port_conf = port_conf_default;
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
    TLOG() << "Error during getting device (port " << port << ") retval: " << retval;
    return retval;
  }

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0)
    return retval;

  // Set MTU
  rte_eth_dev_set_mtu(port, 9000);//RTE_JUMBO_ETHER_MTU);
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
    retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool[q].get());
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

std::unique_ptr<rte_mempool>
get_mempool(const std::string& pool_name) {
  TLOG() << "RTE_MBUF_DEFAULT_BUF_SIZE = " << RTE_MBUF_DEFAULT_BUF_SIZE;
  TLOG() << "NUM_MBUFS = " << NUM_MBUFS;

  struct rte_mempool *mbuf_pool;
  mbuf_pool = rte_pktmbuf_pool_create(pool_name.c_str(), NUM_MBUFS, //* nb_ports,
    //MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    MBUF_CACHE_SIZE, 0, 9800, 4); //rte_socket_id()); // RX packet length(9618) with head-room(128) = 9746 

  if (mbuf_pool == NULL) {
    // ers fatal
    rte_exit(EXIT_FAILURE, "ERROR: Cannot create rte_mempool!\n");
  }
  return std::unique_ptr<rte_mempool>(mbuf_pool);
}

std::vector<char*>
string_to_eal_args(const std::string& params)
{
  auto parts = boost::program_options::split_unix(params);
  std::vector<char*> cstrings;
  for(auto& str : parts){
    cstrings.push_back(const_cast<char*> (str.c_str()));
  }
  return cstrings;
}

void
init_eal(int argc, char* argv[]) {

  // Init EAL
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
  }
  TLOG() << "EAL initialized with provided parameters.";
}

int
get_available_ports() {
  // Check that there is an even number of ports to send/receive on
  unsigned nb_ports;
  nb_ports = rte_eth_dev_count_avail();
  TLOG() << "Available PORTS: " << nb_ports;
  if (nb_ports < 2 || (nb_ports & 1)) {
    rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
  }

  return nb_ports;
}

int 
wait_for_lcores() {
  int lcore_id;
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (rte_eal_wait_lcore(lcore_id) < 0) {
      return -1;
    }
  }
  return 0;
}

void finish_eal() {
  rte_eal_cleanup();
}

} // namespace ealutils
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_
