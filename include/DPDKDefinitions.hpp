/**
 * @file DPDKDefinitions.hpp DPDK related constants and defines
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef DPDKLIBS_SRC_DPDKDEFINITIONS_HPP_
#define DPDKLIBS_SRC_DPDKDEFINITIONS_HPP_

namespace dunedaq {
namespace dpdklibs {

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_DPDKDEFINITIONS_HPP_