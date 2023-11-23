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
#include "iomanager/queue/FollyHPSPSCQueue.hpp"
#include "utilities/ReusableThread.hpp"

// The Payload
// const constexpr std::size_t PAYLOAD_SIZE = 8192; // for 12: 5568
const constexpr std::size_t PAYLOAD_SIZE = 7200; // for 12: 5568
struct PayloadFrame
{
  char data[PAYLOAD_SIZE];
};


using namespace dunedaq::readoutlibs;
using namespace dunedaq::utilities;


enum class SPSCQueueType : int { DynamicUnbound, ProducerConsumer };


int
main(int argc, char** argv)
{

  std::map<std::string, SPSCQueueType> map{{"dub", SPSCQueueType::DynamicUnbound}, {"pc", SPSCQueueType::ProducerConsumer}};


  int runsecs = 120;
  int queue_size = 1000;
  SPSCQueueType qtype{SPSCQueueType::ProducerConsumer};

  CLI::App app{"Queue performance app"};
  app.add_option("-s", queue_size, "Queue size");
  app.add_option("-t", runsecs, "Run Time");
    app.add_option("-q,--queue-type", qtype, "Queue Type, DynamiUnbound (dub) or ProducerConsumer (pc)")
        ->transform(CLI::CheckedTransformer(map, CLI::ignore_case));
  CLI11_PARSE(app, argc, argv);


  std::unique_ptr<dunedaq::iomanager::Queue<PayloadFrame>> queue;
  switch (qtype)
  {
    case SPSCQueueType::ProducerConsumer:
    queue.reset(new dunedaq::iomanager::FollyHPSPSCQueue<PayloadFrame>("spsc", queue_size));
    break;
  case SPSCQueueType::DynamicUnbound:
    queue.reset(new dunedaq::iomanager::FollySPSCQueue<PayloadFrame>("spsc", queue_size));
    break;
  
  default:
    break;
  }
  // dunedaq::iomanager::FollySPSCQueue<PayloadFrame> queue("spsc", queue_size);
  // dunedaq::iomanager::FollyHPSPSCQueue<PayloadFrame> queue("spsc", queue_size);

  // PayloadFrame frame;
  // // std::cout << "Pop, timeout 10s" << std::endl;
  // // bool timed_out = queue.try_pop(frame, std::chrono::seconds(10));
  // // std::cout << "Pop: timed out " << timed_out << std::endl;


  // std::cout << "Push, loading queue" << std::endl;
  // for(uint i(1); i<queue.get_capacity(); ++i) {
  //   std::cout << "- " << i <<std::endl;
  //   queue.try_push(std::move(frame), std::chrono::seconds(60));
  // }

  // for(uint i(1); i<100000; ++i) {
  //   std::cout << "Push (" << i << "), timeout 60s" << std::endl;
  //   bool timed_out = queue.try_push(std::move(frame), std::chrono::seconds(60));
  // }


  // return 0;


  // Run marker
  std::atomic<bool> marker{ true };

  // RateLimiter
  TLOG() << "Creating ratelimiter with 1MHz...";
  RateLimiter rl(1000);

  // Counter for ops/s
  std::atomic<int> newops = 0;
  std::atomic<int> pushes = 0;
  std::atomic<int> pops = 0;

  std::chrono::milliseconds timeout = std::chrono::milliseconds::zero();

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
      pops += queue->try_pop(frame, timeout);
    }
    TLOG() << "Popper done";
  };

  auto pusher = ReusableThread(0);
  pusher.set_name("pusher", 0);
  auto pusher_func = [&]() {
    PayloadFrame frame;
    while (marker) {
      pushes += queue->try_push(std::move(frame), timeout);
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
