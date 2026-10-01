#define BOOST_TEST_MODULE DescriptorFork_test
#include "boost/test/unit_test.hpp"
#include "../src/FrameWorker.hpp"
#include "fdreadoutlibs/pds/DescriptorProcessor.hpp"
#include <future>
#include <mutex>

using dunedaq::dpdklibs::FrameWorker;
using namespace dunedaq::fdreadoutlibs::pds;

BOOST_AUTO_TEST_CASE(tp_progress_is_independent_of_raw_saturation)
{
  std::promise<void> entered, release;
  auto released = release.get_future().share();
  std::atomic<bool> first{true};
  FrameWorker raw(-1, 1, 2, [&](void*, char*, std::size_t) {
    if (first.exchange(false)) { entered.set_value(); released.wait(); }
  });
  std::mutex mutex;
  uint64_t previous[2]{}, counts[2]{};
  bool ordered = true;
  DescriptorProcessor processor([](const Frame& f) { return f.get_channel(); }, {}, 0,
    [&](std::vector<TP>&& tps) {
      std::lock_guard lock(mutex);
      for (auto& tp : tps) {
        ordered &= tp.time_start > previous[tp.channel];
        previous[tp.channel] = tp.time_start;
        ++counts[tp.channel];
      }
      return true;
    });
  auto process = [&](void*, char* bytes, std::size_t size) {
    if (size != sizeof(DescriptorFrame)) throw std::logic_error("Waveform leaked into descriptor queue");
    DescriptorFrame frame;
    std::memcpy(&frame, bytes, sizeof(frame));
    processor.process(frame);
  };
  FrameWorker tp0(-1, 1, 16, process), tp1(-1, 1, 16, process);
  Frame frame{};
  raw.enqueue(0, nullptr, reinterpret_cast<char*>(&frame), sizeof(frame));
  raw.flush(0);
  entered.get_future().wait();
  for (unsigned i = 0; i < 1000; ++i) {
    frame.daq_header.timestamp = 1000 + i;
    frame.header.version = Frame::version;
    frame.header.fragment_descriptor = 1;
    frame.header.channel = i % 2;
    auto& peak = frame.header.peaks_data.peaks[0];
    peak.found = 1; peak.adc_peak = 10; peak.adc_integral = 10;
    DescriptorFrame descriptor{frame.daq_header, frame.header};
    auto& tp = i % 2 ? tp1 : tp0;
    tp.enqueue(0, nullptr, reinterpret_cast<char*>(&descriptor), sizeof(descriptor));
    raw.enqueue(0, nullptr, reinterpret_cast<char*>(&frame), sizeof(frame));
    raw.flush(0);
  }
  tp0.flush(0); tp1.flush(0);
  tp0.drain(); tp1.drain();
  // TP completion is observed while the raw callback is still blocked.
  BOOST_CHECK_EQUAL(processor.counters.sent.load(), 1000);
  BOOST_CHECK_EQUAL(tp0.counters.dropped.load() + tp1.counters.dropped.load(), 0);
  BOOST_CHECK_EQUAL(counts[0], 500);
  BOOST_CHECK_EQUAL(counts[1], 500);
  BOOST_CHECK(ordered);
  BOOST_CHECK_GT(raw.counters.dropped.load(), 0);
  release.set_value();
  raw.drain();
  BOOST_CHECK_EQUAL(tp0.counters.failed.load() + tp1.counters.failed.load(), 0);
}
