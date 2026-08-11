/**
 * @file DpdkTxBackend.hpp
 *
 * Backend implementing the TxEngine.hpp concept over the DPDK TX API:
 * rte_pktmbuf_alloc, rte_pktmbuf_append, rte_eth_tx_burst, rte_pktmbuf_free.
 * prepare() uses rte_pktmbuf_append, which is bounded by the mbuf tailroom, so
 * an undersized data room fails instead of writing past the data area.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_DPDKTXBACKEND_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_DPDKTXBACKEND_HPP_

#include <rte_mempool.h>

#include <cstddef>
#include <cstdint>

namespace dunedaq::dpdklibs::sender {

struct DpdkTxBackend
{
  rte_mempool* pool = nullptr;
  std::uint16_t port = 0;
  std::uint16_t queue = 0;

  // Returns nullptr if the pool is exhausted.
  void* alloc();

  // Appends bytes to the mbuf and sets l2_len, l3_len and l4_len.  Returns the
  // start of the appended region, or nullptr if the tailroom is smaller than
  // bytes.
  std::uint8_t* prepare(void* buf, std::size_t bytes);

  // Enqueues one mbuf.  Returns false if the TX ring did not accept it, in
  // which case the caller still owns buf.
  bool transmit(void* buf);

  void free(void* buf);
};

} // namespace dunedaq::dpdklibs::sender

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_DPDKTXBACKEND_HPP_
