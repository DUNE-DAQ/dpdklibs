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


#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

static volatile uint8_t dpdk_quit_signal; 

std::string get_mac_addr_str(const rte_ether_addr& addr);
  
// Modifies Ethernet device configuration to multi-queue RSS with offload
void iface_conf_rss_mode(struct rte_eth_conf& iface_conf, bool mode = false, bool offload = false);

// Enables RX in promiscuous mode for the Ethernet device.
int iface_promiscuous_mode(std::uint16_t iface, bool mode = false);

int iface_init(uint16_t iface, uint16_t rx_rings, uint16_t tx_rings,
           uint16_t rx_ring_size, uint16_t tx_ring_size,
           std::map<int, std::unique_ptr<rte_mempool>>& mbuf_pool,
           bool with_reset=false, bool with_mq_rss=false, bool check_link_status=false);

std::unique_ptr<rte_mempool> get_mempool(const std::string& pool_name, 
            int num_mbufs=NUM_MBUFS, int mbuf_cache_size=MBUF_CACHE_SIZE,
            int data_room_size=9800, int socket_id=0);

std::vector<const char*> construct_eal_argv(const std::vector<std::string> &std_argv);

void init_eal(int argc, const char* argv[]);

void init_eal( const std::vector<std::string>& args );

int get_available_ifaces();

int wait_for_lcores();

void finish_eal();


} // namespace ealutils
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_EALSETUP_HPP_
