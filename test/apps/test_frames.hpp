#ifndef DPDKLIBS_TEST_APPS_TEST_FRAMES_HPP_
#define DPDKLIBS_TEST_APPS_TEST_FRAMES_HPP_

#include "rte_ether.h"

#include <cstdint>

// Many of the values below are expressed as types other than the
// standard "int" in order to directly adhere to the dpdk library
// function signatures

namespace dunedaq::dpdklibs {

  // Apparently only 8 and above works for "burst_size"

  // From the dpdk documentation, describing the rte_eth_rx_burst
  // function (and keeping in mind that their "nb_pkts" variable is the
  // same as our "burst size" variable below):
  // "Some drivers using vector instructions require that nb_pkts is
  // divisible by 4 or 8, depending on the driver implementation."
  
  constexpr unsigned g_burst_size = 256;

  constexpr int g_expected_packet_size = 7188;   // i.e., every packet that isn't the initial one
  constexpr uint32_t g_expected_packet_type = 0x291;

  constexpr uint16_t g_default_mbuf_size = 9000;  // As opposed to RTE_MBUF_DEFAULT_BUF_SIZE

  constexpr uint16_t g_rx_ring_size = 1024;
  constexpr uint16_t g_tx_ring_size = 1024;

  constexpr unsigned g_num_mbufs = 8191;
  constexpr unsigned g_mbuf_cache_size = 250;

  constexpr uint16_t g_pg_jumbo_frame_len = 9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN;

#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (g_pg_jumbo_frame_len - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif
  constexpr int g_rte_jumbo_ether_mtu = RTE_JUMBO_ETHER_MTU;
  
} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_TEST_APPS_TEST_FRAMES_HPP_
