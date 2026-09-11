/**
 * @file DpdkOwnership_test.cxx
 *
 * Ownership tests for the DPDK mempool handles: the unique_mempool transfer
 * contract and the BorrowedPoolMap release-without-delete guarantee.  All
 * tests run without EAL.
 *
 * No deleter may run on a fake pool address.  A missing release would invoke a
 * deleter on static storage and terminate the process.  Completion of the test
 * is therefore the assertion.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "dpdklibs/DpdkMempool.hpp"

#define BOOST_TEST_MODULE DpdkOwnership_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <array>
#include <memory>
#include <utility>

using namespace dunedaq::dpdklibs;

namespace {

// Static storage used as stand-in rte_mempool addresses.
alignas(8) std::array<char, 8> g_fake_pool_a;
alignas(8) std::array<char, 8> g_fake_pool_b;

rte_mempool*
fake_pool_a()
{
  return reinterpret_cast<rte_mempool*>(g_fake_pool_a.data()); // NOLINT
}

rte_mempool*
fake_pool_b()
{
  return reinterpret_cast<rte_mempool*>(g_fake_pool_b.data()); // NOLINT
}

} // namespace ""

BOOST_AUTO_TEST_SUITE(DpdkOwnership_test)

BOOST_AUTO_TEST_CASE(MempoolDeleterIgnoresNullptr)
{
  MempoolDeleter deleter;
  deleter(nullptr); // must be a no-op, not a call to rte_mempool_free

  unique_mempool empty;
  BOOST_CHECK(empty.get() == nullptr);
  // empty is destroyed here; the deleter must not call DPDK for a null handle
}

BOOST_AUTO_TEST_CASE(TakeMempoolOwnershipTransfersWithoutDeleting)
{
  std::unique_ptr<rte_mempool> legacy(fake_pool_a());
  auto owned = take_mempool_ownership(std::move(legacy));

  // Ownership moved.  The legacy handle is empty, so its default deleter
  // (operator delete, incorrect for rte_mempool) cannot run.
  BOOST_CHECK(legacy.get() == nullptr);
  BOOST_CHECK_EQUAL(static_cast<void*>(owned.get()), static_cast<void*>(fake_pool_a()));

  // Detach before scope exit so MempoolDeleter is not given the fake address.
  owned.release(); // NOLINT(bugprone-unused-return-value)
}

BOOST_AUTO_TEST_CASE(BorrowedPoolMapFeedsLegacyMapAndReleasesOnDestruction)
{
  {
    BorrowedPoolMap borrowed;
    borrowed.borrow(0, fake_pool_a());
    borrowed.borrow(3, fake_pool_b());

    auto& legacy_map = borrowed.get();
    BOOST_REQUIRE_EQUAL(legacy_map.size(), 2);
    BOOST_CHECK_EQUAL(static_cast<void*>(legacy_map.at(0).get()), static_cast<void*>(fake_pool_a()));
    BOOST_CHECK_EQUAL(static_cast<void*>(legacy_map.at(3).get()), static_cast<void*>(fake_pool_b()));
  }
  // The map was destroyed with both entries populated.  Reaching this line
  // means every entry was released rather than deleted.
  BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
