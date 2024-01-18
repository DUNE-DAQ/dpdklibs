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

#include "dpdklibs/EALSetup.hpp"
#include <rte_eal.h>
#include <rte_ethdev.h>

namespace dunedaq {
namespace dpdklibs {
namespace ealutils {

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

// static volatile uint8_t dpdk_quit_signal; 

static const struct rte_eth_conf iface_conf_default = {
  .rxmode = {
    .mtu = 9000,
    .max_lro_pkt_size = 9000,
    //.split_hdr_size = 0, // deprecated in dpdk@22.10
    .offloads = (RTE_ETH_RX_OFFLOAD_TIMESTAMP 
               | RTE_ETH_RX_OFFLOAD_IPV4_CKSUM
               | RTE_ETH_RX_OFFLOAD_UDP_CKSUM),
  },

  .txmode = {
    .offloads = (RTE_ETH_TX_OFFLOAD_MULTI_SEGS),
  },
};


std::string get_mac_addr_str(const rte_ether_addr& addr) {
  std::stringstream macstr;
  macstr << std::hex << static_cast<int>(addr.addr_bytes[0]) << ":" << static_cast<int>(addr.addr_bytes[1]) << ":" << static_cast<int>(addr.addr_bytes[2]) << ":" << static_cast<int>(addr.addr_bytes[3]) << ":" << static_cast<int>(addr.addr_bytes[4]) << ":" << static_cast<int>(addr.addr_bytes[5]) << std::dec;  
  return macstr.str();
}

  
// Modifies Ethernet device configuration to multi-queue RSS with offload
void
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
int
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



int
iface_init(uint16_t iface, uint16_t rx_rings, uint16_t tx_rings,
           uint16_t rx_ring_size, uint16_t tx_ring_size,
           std::map<int, std::unique_ptr<rte_mempool>>& mbuf_pool,
           bool with_reset, bool with_mq_rss)
{
  struct rte_eth_conf iface_conf = iface_conf_default;
  uint16_t nb_rxd = rx_ring_size;
  uint16_t nb_txd = tx_ring_size;
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

  TLOG() << "Iface " << iface << " RX Ring info :" 
    << " min " << dev_info.rx_desc_lim.nb_min 
    << " max " << dev_info.rx_desc_lim.nb_max 
    << " align " << dev_info.rx_desc_lim.nb_align 
  ;

  // Carry out a reset of the interface
  if (with_reset) {
    retval = rte_eth_dev_reset(iface);
    if (retval != 0) {
      TLOG() << "Resetting device (iface " << iface << ") failed. Retval: " << retval;
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

  // These values influenced by Sec. 8.4.4 of https://doc.dpdk.org/guides-1.8/prog_guide/poll_mode_drv.html
  txconf.tx_rs_thresh = 32; 
  txconf.tx_free_thresh = 32;
  txconf.tx_thresh.wthresh = 0;
  
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
  if (retval == 0) {
    TLOG() << "MAC address: " << get_mac_addr_str(addr);
  } else {
    return retval;
  }

  // Get interface info
  retval = rte_eth_dev_info_get(iface, &dev_info);
  if (retval != 0) {
    TLOG() << "Error during getting device (iface " << iface << ") retval: " << retval;
    return retval;
  }

  TLOG() << "Iface[" << iface << "] Rx Ring info:"
    << " min=" << dev_info.rx_desc_lim.nb_min 
    << " max=" << dev_info.rx_desc_lim.nb_max 
    << " align=" << dev_info.rx_desc_lim.nb_align;
  TLOG() << "Iface[" << iface << "] Tx Ring info:" 
    << " min=" << dev_info.rx_desc_lim.nb_min 
    << " max=" << dev_info.rx_desc_lim.nb_max 
    << " align=" << dev_info.rx_desc_lim.nb_align;

  for (size_t j = 0; j < dev_info.nb_rx_queues; j++) {

    struct rte_eth_rxq_info queue_info;
    int count;

    retval = rte_eth_rx_queue_info_get(iface, j, &queue_info);
    if (retval != 0)
      break;

    count = rte_eth_rx_queue_count(iface, j);
    TLOG() << "rx[" << j << "] descriptors=" << count << "/" << queue_info.nb_desc
           << " scattered=" << (queue_info.scattered_rx ? "yes" : "no")
           << " conf.drop_en=" << (queue_info.conf.rx_drop_en ? "yes" : "no")
           << " conf.rx_deferred_start=" << (queue_info.conf.rx_deferred_start ? "yes" : "no")
           << " rx_buf_size=" << queue_info.rx_buf_size;
  }

  return 0;
}

std::unique_ptr<rte_mempool>
get_mempool(const std::string& pool_name, 
            int num_mbufs, int mbuf_cache_size,
            int data_room_size, int socket_id) {
  TLOG() << "get_mempool with: NUM_MBUFS = " << num_mbufs
         << " | MBUF_CACHE_SIZE = " << mbuf_cache_size
         << " | data_room_size = " << data_room_size
         << " | SOCKET_ID = " << socket_id;

  struct rte_mempool *mbuf_pool;
  mbuf_pool = rte_pktmbuf_pool_create(pool_name.c_str(), num_mbufs, 
    mbuf_cache_size, 0, data_room_size, 
    socket_id); 
  
  if (mbuf_pool == NULL) {
    // ers fatal
    rte_exit(EXIT_FAILURE, "ERROR: Cannot create rte_mempool!\n");
  }
  return std::unique_ptr<rte_mempool>(mbuf_pool);
}

std::vector<const char*> 
construct_eal_argv(const std::vector<std::string> &std_argv){
  std::vector<const char*> vec_argv;
  for (int i=0; i < std_argv.size() ; i++){
      vec_argv.insert(vec_argv.end(), std_argv[i].data());
  }
  return vec_argv;
}



void
init_eal(int argc, const char* argv[]) {

  std::stringstream ss;
  for( size_t i(0); i<argc; ++i) {
    ss << argv[i] << " ";
  }
  TLOG() << "EAL init arguments: " << ss.str();

  // Init EAL
  int ret = rte_eal_init(argc, (char**)argv);
  if (ret < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
  }
  TLOG() << "EAL initialized with provided parameters.";
}

void
init_eal( const std::vector<std::string>& args ) {

  std::vector<const char*> eal_argv = ealutils::construct_eal_argv(args);
  const char** constructed_eal_argv = eal_argv.data();
  int constructed_eal_argc = args.size();
  ealutils::init_eal(constructed_eal_argc, constructed_eal_argv);
}

int
get_available_ifaces() {
  // Check that there is an even number of interfaces to send/receive on
  unsigned nb_ifaces;
  nb_ifaces = rte_eth_dev_count_avail();
  TLOG() << "Available interfaces: " << nb_ifaces;
  return nb_ifaces;
}

int 
wait_for_lcores() {
  int lcore_id;
  int ret = 0;
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    //TLOG() << "Waiting for lcore[" << lcore_id << "] to finish packet processing.";
    ret = rte_eal_wait_lcore(lcore_id);
  }
  return ret;
}

void finish_eal() {
  rte_eal_cleanup();
}

} // namespace ealutils
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_
