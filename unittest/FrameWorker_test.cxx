#define BOOST_TEST_MODULE FrameWorker_test
#include "boost/test/unit_test.hpp"
#include "../src/FrameWorker.hpp"
#include <chrono>
#include <future>
#include <stdexcept>

using dunedaq::dpdklibs::FrameWorker;

BOOST_AUTO_TEST_CASE(owns_bytes_preserves_order_and_drains)
{
  std::array<uint64_t, 2> next{};
  FrameWorker worker(-1, 2, 8, [&](void* target, char* bytes, std::size_t size) {
    uint64_t value;
    std::memcpy(&value, bytes, size);
    auto producer = reinterpret_cast<std::uintptr_t>(target);
    if (value != next[producer]++) throw std::runtime_error("Frame order changed");
  });
  std::vector<std::thread> producers;
  for (std::size_t p = 0; p < 2; ++p) producers.emplace_back([&, p] {
    for (uint64_t i = 0; i < 10000; ++i) {
      auto value = i;
      while (!worker.enqueue(p, reinterpret_cast<void*>(p), reinterpret_cast<char*>(&value), sizeof(value))) {
        worker.flush(p);
        std::this_thread::yield();
      }
      value = ~i;
      if (i % 17 == 16) worker.flush(p);
    }
    worker.flush(p);
  });
  for (auto& producer : producers) producer.join();
  worker.drain();
  BOOST_CHECK_EQUAL(worker.counters.enqueued.load(), 20000);
  BOOST_CHECK_EQUAL(worker.counters.processed.load(), 20000);
  BOOST_CHECK_EQUAL(worker.counters.failed.load(), 0);
  BOOST_CHECK_EQUAL(next[0], 10000);
  BOOST_CHECK_EQUAL(next[1], 10000);
  BOOST_CHECK_EQUAL(worker.pending(), 0);
}

BOOST_AUTO_TEST_CASE(bounded_overload_and_callback_failure)
{
  std::promise<void> entered, release;
  auto released = release.get_future().share();
  std::atomic<bool> first{true};
  FrameWorker worker(-1, 1, 2, [&](void*, char*, std::size_t) {
    if (first.exchange(false)) { entered.set_value(); released.wait(); }
    throw std::runtime_error("Rejected callback");
  });
  char bytes[16384]{};
  BOOST_REQUIRE(worker.enqueue(0, nullptr, bytes, sizeof(bytes)));
  worker.flush(0);
  entered.get_future().wait();
  BOOST_REQUIRE(worker.enqueue(0, nullptr, bytes, sizeof(bytes)));
  worker.flush(0);
  BOOST_CHECK(!worker.enqueue(0, nullptr, bytes, sizeof(bytes)));
  BOOST_CHECK_EQUAL(worker.counters.dropped.load(), 1);
  BOOST_CHECK_EQUAL(worker.counters.high_water.load(), 2);
  release.set_value();
  worker.stop();
  BOOST_CHECK_EQUAL(worker.counters.failed.load(), 2);
  BOOST_CHECK_EQUAL(worker.pending(), 0);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_configuration)
{
  auto handler = [](void*, char*, std::size_t) {};
  BOOST_CHECK_THROW(FrameWorker(-1, 1, 3, handler), std::invalid_argument);
  BOOST_CHECK_THROW(FrameWorker(CPU_SETSIZE, 1, 2, handler), std::invalid_argument);
}
