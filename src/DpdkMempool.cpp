/**
 * @file DpdkMempool.cpp Implementation of the rte_mempool ownership handles.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/DpdkMempool.hpp"

#include <rte_mempool.h>

#include <map>
#include <memory>

namespace dunedaq::dpdklibs {

void
MempoolDeleter::operator()(rte_mempool* pool) const noexcept
{
  if (pool != nullptr) {
    rte_mempool_free(pool);
  }
}

unique_mempool
take_mempool_ownership(std::unique_ptr<rte_mempool>&& legacy)
{
  return unique_mempool(legacy.release());
}

BorrowedPoolMap::~BorrowedPoolMap()
{
  for (auto& [index, pool] : m_map) { // NOLINT(readability-qualified-auto)
    pool.release(); // NOLINT(bugprone-unused-return-value) borrowed; not owned by this map
  }
}

void
BorrowedPoolMap::borrow(int index, rte_mempool* pool)
{
  m_map[index].reset(pool);
}

std::map<int, std::unique_ptr<rte_mempool>>&
BorrowedPoolMap::get()
{
  return m_map;
}

} // namespace dunedaq::dpdklibs
