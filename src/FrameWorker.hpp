#ifndef DPDKLIBS_SRC_FRAMEWORKER_HPP_
#define DPDKLIBS_SRC_FRAMEWORKER_HPP_

#include <array>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <pthread.h>
#include <sched.h>

namespace dunedaq::dpdklibs {

// One RX producer per queue; a worker owns the consumer and copied frame bytes.
class FrameWorker
{
public:
  using Handler = std::function<void(void*, char*, std::size_t)>;
  struct Counters {
    std::atomic<uint64_t> enqueued{0}, processed{0}, dropped{0}, failed{0}, high_water{0}, max_queue_wait_ns{0};
  } counters;

  FrameWorker(int cpu, std::size_t producers, std::size_t capacity, Handler handler)
    : m_handler(std::move(handler))
  {
    if (!producers || capacity < 2 || (capacity & (capacity - 1))) {
      throw std::invalid_argument("Frame worker queue capacity must be a power of two >= 2");
    }
    std::promise<void> ready;
    auto result = ready.get_future();
    m_thread = std::thread([this, cpu, producers, capacity, ready = std::move(ready)]() mutable {
      try {
        char name[16];
        std::snprintf(name, sizeof(name), "frame-cpu-%d", cpu);
        pthread_setname_np(pthread_self(), name);
        if (cpu >= 0) {
          if (cpu >= CPU_SETSIZE) throw std::invalid_argument("Frame worker CPU exceeds CPU_SETSIZE");
          cpu_set_t cpus;
          CPU_ZERO(&cpus);
          CPU_SET(cpu, &cpus);
          if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus)) {
            throw std::runtime_error("Cannot bind frame worker CPU");
          }
        }
        // First touch on the processing core places bounded storage on its NUMA node.
        for (std::size_t i = 0; i < producers; ++i) m_queues.push_back(std::make_unique<Queue>(capacity));
        ready.set_value();
      } catch (...) {
        ready.set_exception(std::current_exception());
        return;
      }
      while (!m_quit.load(std::memory_order_acquire) || pending()) {
        bool worked = false;
        for (auto& queue : m_queues) {
          auto& q = *queue;
          auto read = q.read.load(std::memory_order_relaxed);
          if (read == q.write.load(std::memory_order_acquire)) continue;
          auto& batch = q.batches[read & q.mask];
          const auto wait = now_ns() - batch.published_ns;
          auto maximum = counters.max_queue_wait_ns.load(std::memory_order_relaxed);
          while (maximum < wait && !counters.max_queue_wait_ns.compare_exchange_weak(maximum, wait)) {}
          for (std::size_t j = 0; j < batch.count; ++j) {
            auto& frame = batch.frames[j];
            try {
              m_handler(frame.target, batch.bytes.data() + frame.offset, frame.size);
              ++counters.processed;
            } catch (...) {
              ++counters.failed;
            }
          }
          q.read.store(read + 1, std::memory_order_release);
          worked = true;
        }
        if (!worked) std::this_thread::yield();
      }
    });
    try { result.get(); }
    catch (...) { m_thread.join(); throw; }
  }

  ~FrameWorker() { stop(); }
  FrameWorker(const FrameWorker&) = delete;
  FrameWorker& operator=(const FrameWorker&) = delete;

  bool enqueue(std::size_t producer, void* target, const char* bytes, std::size_t size)
  {
    auto& q = *m_queues.at(producer);
    if (size > kBytes) { ++counters.dropped; return false; }
    if (q.count && (q.count == kFrames || q.used + size > kBytes)) flush(producer);
    const auto write = q.write.load(std::memory_order_relaxed);
    if (write - q.read.load(std::memory_order_acquire) == q.capacity) {
      ++counters.dropped;
      return false;
    }
    auto& batch = q.batches[write & q.mask];
    batch.frames[q.count++] = {target, q.used, size};
    std::memcpy(batch.bytes.data() + q.used, bytes, size);
    q.used += size;
    return true;
  }

  void flush(std::size_t producer)
  {
    auto& q = *m_queues.at(producer);
    if (!q.count) return;
    auto write = q.write.load(std::memory_order_relaxed);
    q.batches[write & q.mask].count = q.count;
    q.batches[write & q.mask].published_ns = now_ns();
    counters.enqueued.fetch_add(q.count, std::memory_order_relaxed);
    q.count = q.used = 0;
    const auto occupancy = write + 1 - q.read.load(std::memory_order_acquire);
    auto high = counters.high_water.load(std::memory_order_relaxed);
    while (high < occupancy && !counters.high_water.compare_exchange_weak(high, occupancy)) {}
    q.write.store(write + 1, std::memory_order_release);
  }

  uint64_t pending() const
  {
    uint64_t total = 0;
    for (auto& q : m_queues) {
      // Load read first: publication can advance while this snapshot is taken.
      auto read = q->read.load(std::memory_order_acquire);
      total += q->write.load(std::memory_order_acquire) - read;
    }
    return total;
  }

  // Call only after every RX producer has flushed and quiesced.
  void drain() const { while (pending()) std::this_thread::yield(); }
  void stop()
  {
    m_quit.store(true, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
  }

private:
  static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static constexpr std::size_t kBytes = 16384, kFrames = 128;
  struct Frame { void* target; std::size_t offset, size; };
  struct Batch { std::array<char, kBytes> bytes{}; std::array<Frame, kFrames> frames; std::size_t count; uint64_t published_ns; };
  struct Queue {
    explicit Queue(std::size_t cap) : batches(new Batch[cap]), capacity(cap), mask(cap - 1) {}
    std::unique_ptr<Batch[]> batches;
    const uint64_t capacity, mask;
    alignas(64) std::atomic<uint64_t> write{0};
    alignas(64) std::atomic<uint64_t> read{0};
    std::size_t count{0}, used{0};
  };
  Handler m_handler;
  std::vector<std::unique_ptr<Queue>> m_queues;
  std::atomic<bool> m_quit{false};
  std::thread m_thread;
};

} // namespace dunedaq::dpdklibs
#endif
