/**
 * @file DpdkMempool.hpp
 *
 * Ownership handles for rte_mempool.
 *
 * rte_mempool objects are created by rte_pktmbuf_pool_create and must be
 * released with rte_mempool_free before rte_eal_cleanup.  They must not be
 * passed to operator delete, so std::unique_ptr<rte_mempool> with its default
 * deleter encodes the wrong contract.  This header provides:
 *
 *  - unique_mempool: unique_ptr with the rte_mempool_free deleter.  Destroying
 *    it before EAL cleanup releases the pool name, so a
 *    configure -> scrap -> configure cycle can recreate a pool of the same
 *    name.
 *
 *  - BorrowedPoolMap: non-owning adapter for legacy interfaces
 *    (ealutils::iface_init) that take
 *    std::map<int, std::unique_ptr<rte_mempool>>&.  It releases every entry on
 *    destruction, including on exception paths, so the legacy map's default
 *    deleter cannot run on a live pool.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_DPDKMEMPOOL_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_DPDKMEMPOOL_HPP_

#include <rte_mempool.h>

#include <map>
#include <memory>

namespace dunedaq::dpdklibs {

struct MempoolDeleter
{
  // No-op for a null handle.
  void operator()(rte_mempool* pool) const noexcept;
};

using unique_mempool = std::unique_ptr<rte_mempool, MempoolDeleter>;

// Move a pool from a default-deleting unique_ptr into unique_mempool.  The
// default deleter never runs.
unique_mempool
take_mempool_ownership(std::unique_ptr<rte_mempool>&& legacy);

class BorrowedPoolMap
{
public:
  BorrowedPoolMap() = default;
  BorrowedPoolMap(const BorrowedPoolMap&) = delete;
  BorrowedPoolMap& operator=(const BorrowedPoolMap&) = delete;
  BorrowedPoolMap(BorrowedPoolMap&&) = delete;
  BorrowedPoolMap& operator=(BorrowedPoolMap&&) = delete;

  // Releases every entry.  No deleter runs on a borrowed pool.
  ~BorrowedPoolMap();

  void borrow(int index, rte_mempool* pool);

  std::map<int, std::unique_ptr<rte_mempool>>& get();

private:
  std::map<int, std::unique_ptr<rte_mempool>> m_map;
};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_DPDKMEMPOOL_HPP_
