/**
 * @file test_ratelimiter_app.cxx Test application for
 * ratelimiter implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "readoutlibs/utils/RateLimiter.hpp"

#include "logging/Logging.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <vector>

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include <fmt/core.h>

#include "iomanager/queue/FollyQueue.hpp"
#include "folly/ProducerConsumerQueue.h"
#include "utilities/ReusableThread.hpp"

// The Payload
const constexpr std::size_t PAYLOAD_SIZE = 8192; // for 12: 5568
struct PayloadFrame
{
  char data[PAYLOAD_SIZE];
};


#include <condition_variable>
#include <chrono>
#include "iomanager/queue/Queue.hpp"

// 
template<class T>
class SPSCFollyQueue : public dunedaq::iomanager::Queue<T>
{
public:
  using value_t = T;
  using duration_t = typename dunedaq::iomanager::Queue<T>::duration_t;

  explicit SPSCFollyQueue(const std::string& name, size_t capacity)
    : dunedaq::iomanager::Queue<T>(name)
    , m_queue(capacity)
    , m_capacity(capacity)
  {}

  size_t get_capacity() const noexcept override { return m_capacity; }

  size_t get_num_elements() const noexcept override { return m_queue.sizeGuess(); }

  bool can_pop() const noexcept override { return !m_queue.isEmpty(); }

  void pop(value_t& val, const duration_t& dur) override
  {

    if (!this->pop(val, dur)) {
      throw dunedaq::iomanager::QueueTimeoutExpired(
        ERS_HERE, this->get_name(), "pop", std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
    }
  }
  bool try_pop(value_t& val, const duration_t& dur) override
  {
  
    // if (dur > std::chrono::milliseconds::zero()) {
    //   auto time_to_wait_for_data = (start_time + timeout) - std::chrono::steady_clock::now();
    //   m_no_longer_empty.wait_for(lk, time_to_wait_for_data, [&]() { return this->can_pop(); });
    // }

    // if (!m_queue.read(val)) {
    //   return false;
    // }
    // return true;

    return m_queue.read(val);
  }

  bool can_push() const noexcept override { return !m_queue.isFull(); }

  void push(value_t&& t, const duration_t& dur) override
  {

    // if (!m_queue.write(std::move(t))) {
    if (!this->try_push(std::move(t))) {
      throw dunedaq::iomanager::QueueTimeoutExpired(ERS_HERE, this->get_name(), "push", std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
    }
  }

  bool try_push(value_t&& t, const duration_t& dur) override
  {

    // if (dur > std::chrono::milliseconds::zero()) {
    //   auto time_to_wait_for_space = (start_time + timeout) - std::chrono::steady_clock::now();
    //   m_no_longer_full.wait_for(lk, time_to_wait_for_space, [&]() { return this->can_push(); });
    // }

    if (!m_queue.write(std::move(t))) {
      ers::error(dunedaq::iomanager::QueueTimeoutExpired(ERS_HERE, this->get_name(), "push", std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()));
      return false;
    }
    return true;

  }

  // Delete the copy and move operations
  SPSCFollyQueue(const SPSCFollyQueue&) = delete;
  SPSCFollyQueue& operator=(const SPSCFollyQueue&) = delete;
  SPSCFollyQueue(SPSCFollyQueue&&) = delete;
  SPSCFollyQueue& operator=(SPSCFollyQueue&&) = delete;

private:
  // The boolean argument is `MayBlock`, where "block" appears to mean
  // "make a system call". With `MayBlock` set to false, the queue
  // just spin-waits, so we want true
  folly::ProducerConsumerQueue<T> m_queue;
  size_t m_capacity;

  std::condition_variable m_no_longer_full;
  std::condition_variable m_no_longer_empty;
};


using namespace dunedaq::readoutlibs;
using namespace dunedaq::utilities;

int
main(int argc, char** argv)
{

  int runsecs = 120;

  CLI::App app{"test frame receiver"};
  // app.add_option("-s", expected_packet_size, "Expected frame size");
  // app.add_option("-i", iface, "Interface to init");
  app.add_option("-t", runsecs, "Run Time");
  // app.add_flag("--check-time", check_timestamp, "Report back differences in timestamp");
  // app.add_flag("-p", per_stream_reports, "Detailed per stream reports");
  CLI11_PARSE(app, argc, argv);

  // dunedaq::iomanager::FollySPSCQueue<PayloadFrame> queue("spsc", 10000);
  SPSCFollyQueue<PayloadFrame> queue("spsc", 10000);


  // Run marker
  std::atomic<bool> marker{ true };

  // RateLimiter
  TLOG() << "Creating ratelimiter with 1MHz...";
  RateLimiter rl(1000);

  // Counter for ops/s
  std::atomic<int> newops = 0;
  std::atomic<int> pushes = 0;
  std::atomic<int> pops = 0;


  // Stats
  auto stats = ReusableThread(0);
  stats.set_name("stats", 0);
  auto stats_func = [&]() {
    TLOG() << "Spawned stats thread...";
    while (marker) {
      TLOG() << "push/s ->  " << pushes.exchange(0) << " pop/s ->  " << pops.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  };

 

  auto popper = ReusableThread(0);
  popper.set_name("popper", 0);
  auto popper_func = [&]() {
    PayloadFrame frame;
    while (marker) {
      pops += queue.try_pop(frame, std::chrono::milliseconds::zero());
    }
    TLOG() << "Popper done";
  };


  auto pusher = ReusableThread(0);
  pusher.set_name("pusher", 0);
  auto pusher_func = [&]() {
    PayloadFrame frame;
    while (marker) {
      pushes += queue.try_push(std::move(frame), std::chrono::milliseconds::zero());
    }
    TLOG() << "Pusher done";

  };


  stats.set_work(stats_func);
  popper.set_work(popper_func);
  pusher.set_work(pusher_func);


  // Killswitch that flips the run marker
  auto killswitch = std::thread([&]() {
    TLOG() << "Application will terminate in " << runsecs << "s...";
    std::this_thread::sleep_for(std::chrono::seconds(runsecs));
    marker.store(false);
  });

  // Join local threads
  TLOG() << "Flipping killswitch in order to stop...";
  if (killswitch.joinable()) {
    killswitch.join();
  }

  // Exit
  TLOG() << "Exiting.";
  return 0;
}
