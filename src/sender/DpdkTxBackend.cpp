/**
 * @file DpdkTxBackend.cpp Implementation of the DPDK TX backend.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/sender/DpdkTxBackend.hpp"

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

#include <cstddef>
#include <cstdint>

namespace dunedaq::dpdklibs::sender {

void*
DpdkTxBackend::alloc()
{
  return rte_pktmbuf_alloc(pool);
}

std::uint8_t*
DpdkTxBackend::prepare(void* buf, std::size_t bytes)
{
  auto* mbuf = static_cast<rte_mbuf*>(buf);
  char* dst = rte_pktmbuf_append(mbuf, static_cast<std::uint16_t>(bytes));
  if (dst == nullptr) {
    return nullptr;
  }
  mbuf->l2_len = static_cast<std::uint16_t>(sizeof(rte_ether_hdr));
  mbuf->l3_len = static_cast<std::uint16_t>(sizeof(rte_ipv4_hdr));
  mbuf->l4_len = static_cast<std::uint16_t>(sizeof(rte_udp_hdr));
  return reinterpret_cast<std::uint8_t*>(dst); // NOLINT
}

bool
DpdkTxBackend::transmit(void* buf)
{
  auto* mbuf = static_cast<rte_mbuf*>(buf);
  rte_mbuf* one_packet = mbuf;
  return rte_eth_tx_burst(port, queue, &one_packet, 1) == 1;
}

void
DpdkTxBackend::free(void* buf)
{
  rte_pktmbuf_free(static_cast<rte_mbuf*>(buf));
}

} // namespace dunedaq::dpdklibs::sender
