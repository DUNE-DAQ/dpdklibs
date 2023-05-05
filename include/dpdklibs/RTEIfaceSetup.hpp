/**
 * @file RTEIfaceSetup.hpp RTE Interface setup functions for DPDK
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_RTEIFACESETUP_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_RTEIFACESETUP_HPP_

#include "logging/Logging.hpp"

#include <rte_eal.h>
#include <rte_ethdev.h>

#include <algorithm>
#include <string>

namespace dunedaq {
namespace dpdklibs {
namespace ifaceutils {

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

static volatile uint8_t dpdk_quit_signal; 

static const struct rte_eth_conf iface_conf_default = {
  .rxmode = {
    .mtu = 9000,
    .max_lro_pkt_size = 9000,
    .split_hdr_size = 0,
    .offloads = (RTE_ETH_RX_OFFLOAD_TIMESTAMP 
               | RTE_ETH_RX_OFFLOAD_IPV4_CKSUM
               | RTE_ETH_RX_OFFLOAD_UDP_CKSUM),
  },

  .txmode = {
    .offloads = (RTE_ETH_TX_OFFLOAD_MULTI_SEGS),
  },
};

// Get number of available interfaces
inline int
get_num_available_ifaces() {
  unsigned nb_ifaces = rte_eth_dev_count_avail();
  TLOG() << "Available interfaces: " << nb_ifaces;
  return nb_ifaces;
}

// Modifies Ethernet device configuration to multi-queue RSS with offload
inline void
iface_conf_rss_mode(struct rte_eth_conf& iface_conf, bool mode = false, bool offload = false)
{
  if (mode) {
    iface_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    if (offload) {
      iface_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_RSS_HASH;
    }
  } else {
    iface_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
  }
}

// Enables RX in promiscuous mode for the Ethernet device.
inline int
iface_promiscuous_mode(std::uint16_t iface, bool mode = false) 
{
  int retval = -1;
  retval = rte_eth_promiscuous_get(iface);
  TLOG() << "Before modification attempt, promiscuous mode is: " << retval;
  if (mode) {
    retval = rte_eth_promiscuous_enable(iface);
  } else {
    retval = rte_eth_promiscuous_disable(iface); 
  }
  if (retval != 0) {
    TLOG() << "Couldn't modify promiscuous mode of iface[" << iface << "]! Error code: " << retval;
  }
  retval = rte_eth_promiscuous_get(iface);
  TLOG() << "New promiscuous mode of iface[" << iface << "] is: " << retval;
  return retval; 
}

// Get interface validity
inline bool
iface_valid(uint16_t iface)
{
  return rte_eth_dev_is_valid_port(iface);
}

inline void
hex_digits_to_stream(std::ostringstream& ostrs, int value, char separator = ' ', char fill = '0', int digits = 2) {
  ostrs << std::setfill(fill) << std::setw(digits) << std::hex << value << std::dec << separator;
}

// Get interface MAC address
inline std::string
get_iface_mac_str(uint16_t iface)
{
  int retval = -1;
  struct rte_ether_addr mac_addr;
  retval = rte_eth_macaddr_get(iface, &mac_addr);
  if (retval != 0) {
    TLOG() << "Failed to get MAC address of interface! Err id: " << retval;
    return std::string("");
  } else {
    std::ostringstream ostrs;
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[0], ':');
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[1], ':');
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[2], ':');
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[3], ':');
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[4], ':');
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[5]);
    std::string mac_str = ostrs.str();
    mac_str.erase(std::remove(mac_str.begin(), mac_str.end(), ' '), mac_str.end());  
    return mac_str;
  }
}

inline int
iface_reset(uint16_t iface)
{
  int retval = -1;
  struct rte_eth_dev_info dev_info;

  // Get interface validity
  if (!rte_eth_dev_is_valid_port(iface)) {
    TLOG() << "Specified interface " << iface << " is not valid in EAL!";
    return retval;
  }

  // Carry out a reset of the interface
  retval = rte_eth_dev_reset(iface);
  if (retval != 0) {
    TLOG() << "Error during resetting device (iface " << iface << ") retval: " << retval;
    return retval;
  }

  return retval;
}

inline int
iface_init(uint16_t iface, uint16_t rx_rings, uint16_t tx_rings, 
	         std::map<int, std::unique_ptr<rte_mempool>>& mbuf_pool,
           bool with_reset=false, bool with_mq_rss=false)
{
  struct rte_eth_conf iface_conf = iface_conf_default;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval = -1;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf txconf;

  // Get interface validity
  if (!rte_eth_dev_is_valid_port(iface)) {
    TLOG() << "Specified interface " << iface << " is not valid in EAL!";
    return retval;
  }
  
  // Get interface info
  retval = rte_eth_dev_info_get(iface, &dev_info);
  if (retval != 0) {
    TLOG() << "Error during getting device (iface " << iface << ") retval: " << retval;
    return retval;
  }

  // Carry out a reset of the interface
  if (with_reset) {
    retval = rte_eth_dev_reset(iface);
    if (retval != 0) {
      TLOG() << "Error during resetting device (iface " << iface << ") retval: " << retval;
      return retval;
    }
  }

  // Should we configure MQ RSS and offload?
  if (with_mq_rss) {
    iface_conf_rss_mode(iface_conf, true, true); // with_rss, with_offload
    // RSS
    if ((iface_conf.rxmode.mq_mode & RTE_ETH_MQ_RX_RSS_FLAG) != 0) {
      TLOG() << "Ethdev port config prepared with RX RSS mq_mode!";
      if ((iface_conf.rxmode.offloads & RTE_ETH_RX_OFFLOAD_RSS_HASH) != 0) {
        TLOG() << "Ethdev port config prepared with RX RSS mq_mode with offloading is requested!";
      }
    }
  }

  // Configure the Ethernet interface
  retval = rte_eth_dev_configure(iface, rx_rings, tx_rings, &iface_conf);
  if (retval != 0)
    return retval;

  // Set MTU of interface
  rte_eth_dev_set_mtu(iface, RTE_JUMBO_ETHER_MTU);
  {
    uint16_t mtu;
    rte_eth_dev_get_mtu(iface, &mtu);
    TLOG() << "Interface: " << iface << " MTU: " << mtu;
  }

  // Adjust RX/TX ring sizes
  retval = rte_eth_dev_adjust_nb_rx_tx_desc(iface, &nb_rxd, &nb_txd);
  if (retval != 0)
    return retval;

  // Allocate and set up RX queues for interface.
  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(iface, q, nb_rxd, rte_eth_dev_socket_id(iface), NULL, mbuf_pool[q].get());
    if (retval < 0)
      return retval;
  }

  txconf = dev_info.default_txconf;
  txconf.offloads = iface_conf.txmode.offloads;
  // Allocate and set up TX queues for interface.
  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(iface, q, nb_txd, rte_eth_dev_socket_id(iface), &txconf);
    if (retval < 0)
      return retval;
  }

  // Start the Ethernet interface.
  retval = rte_eth_dev_start(iface);
  if (retval < 0)
    return retval;

  // Display the interface MAC address.
  struct rte_ether_addr addr;
  retval = rte_eth_macaddr_get(iface, &addr);
  if (retval != 0)
    return retval;

  return 0;
}

} // namespace ifaceutils
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_RTEIFACESETUP_HPP_
