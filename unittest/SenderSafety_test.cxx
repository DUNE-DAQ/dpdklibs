/**
 * @file SenderSafety_test.cxx
 *
 * Failure-injection tests for the sender-safety components: TX engine
 * ownership, the callback-lifetime gate, and the WIBEthTransmitter core
 * (per-stream accounting, registry callbacks, observer hook).  All tests run
 * without EAL or a NIC.
 */

#include "dpdklibs/sender/CallbackGate.hpp"
#include "dpdklibs/sender/TxEngine.hpp"
#include "dpdklibs/sender/WIBEthTransmitter.hpp"

#define BOOST_TEST_MODULE SenderSafety_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace dunedaq::dpdklibs;

namespace {

// Deterministic scriptable backend implementing the TxEngine concept.
struct FakeBackend
{
  int alloc_calls = 0;
  int prepare_calls = 0;
  int tx_calls = 0;
  int free_calls = 0;

  bool fail_alloc = false;
  bool fail_prepare = false;
  bool fail_tx = false;

  std::array<std::uint8_t, 64> storage{};

  void* alloc()
  {
    ++alloc_calls;
    return fail_alloc ? nullptr : storage.data();
  }

  std::uint8_t* prepare(void* buf, std::size_t /*bytes*/)
  {
    ++prepare_calls;
    return fail_prepare ? nullptr : static_cast<std::uint8_t*>(buf);
  }

  bool transmit(void* /*buf*/)
  {
    ++tx_calls;
    return !fail_tx;
  }

  void free(void* /*buf*/) { ++free_calls; }
};

constexpr std::size_t kTestPacketBytes = 16;

// ---------------------------------------------------------------------------
// WIBEthTransmitter fixtures
// ---------------------------------------------------------------------------

// Payload satisfying the transmitter's contract (a `data` member addressing
// one complete WIBEthFrame), mirroring DUNEWIBEthTypeAdapter's layout.
struct FakePayload
{
  char data[wibeth::kWIBEthFrameBytes] = {};
};

// Backend with a full-packet capture buffer, so tests can inspect the bytes
// the transmitter passed to transmit().
struct FakeTxBackend
{
  int alloc_calls = 0;
  int tx_calls = 0;
  int free_calls = 0;
  bool fail_transmit = false;
  std::array<std::uint8_t, wibeth::kEthernetPacketBytes> storage{};

  void* alloc()
  {
    ++alloc_calls;
    return storage.data();
  }
  std::uint8_t* prepare(void* buf, std::size_t /*bytes*/) { return static_cast<std::uint8_t*>(buf); }
  bool transmit(void* /*buf*/)
  {
    ++tx_calls;
    return !fail_transmit;
  }
  void free(void* /*buf*/) { ++free_calls; }
};

struct FakeCallbackConf
{
  std::string uid;
  const std::string& UID() const { return uid; }
};

// Stand-in for the pinned DataMoveCallbackRegistry: identical
// register_callback signature, keyed by conf->UID().  The first registration
// for an ID is retained.
struct FakeCallbackRegistry
{
  std::map<std::string, std::function<void(FakePayload&&)>> callbacks;
  int rejected_duplicates = 0;

  template<typename DataType>
  void register_callback(const FakeCallbackConf* conf, std::function<void(DataType&&)> callback)
  {
    if (callbacks.count(conf->UID()) != 0) {
      ++rejected_duplicates;
      return;
    }
    callbacks[conf->UID()] = std::move(callback);
  }
};

using test_transmitter_t = sender::WIBEthTransmitter<FakeTxBackend>;

dunedaq::detdataformats::DAQEthHeader
read_wire_daq_header(const std::array<std::uint8_t, wibeth::kEthernetPacketBytes>& packet)
{
  dunedaq::detdataformats::DAQEthHeader header;
  std::memcpy(&header, packet.data() + wibeth::kPacketHeaderBytes, sizeof(header));
  return header;
}

} // namespace

BOOST_AUTO_TEST_SUITE(SenderSafety_test)

// ---------------------------------------------------------------------------
// TxEngine ownership and failure matrix (F03/F08)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(EngineAllocFailureFreesNothingAndSkipsBuild)
{
  FakeBackend backend;
  backend.fail_alloc = true;
  bool built = false;
  bool sent = false;

  const auto outcome = sender::send_packet(
    backend, kTestPacketBytes, nullptr, [&](std::uint8_t*) { built = true; }, [&]() { sent = true; });

  BOOST_CHECK(outcome == sender::TxOutcome::kAllocFailed);
  BOOST_CHECK(!built);
  BOOST_CHECK(!sent);
  BOOST_CHECK_EQUAL(backend.prepare_calls, 0);
  BOOST_CHECK_EQUAL(backend.tx_calls, 0);
  BOOST_CHECK_EQUAL(backend.free_calls, 0);
}

BOOST_AUTO_TEST_CASE(EnginePrepareFailureFreesExactlyOnce)
{
  FakeBackend backend;
  backend.fail_prepare = true;
  bool built = false;

  const auto outcome = sender::send_packet(
    backend, kTestPacketBytes, nullptr, [&](std::uint8_t*) { built = true; }, []() {});

  BOOST_CHECK(outcome == sender::TxOutcome::kPrepareFailed);
  BOOST_CHECK(!built);
  BOOST_CHECK_EQUAL(backend.tx_calls, 0);
  BOOST_CHECK_EQUAL(backend.free_calls, 1);
}

BOOST_AUTO_TEST_CASE(EngineSuccessTransfersOwnership)
{
  FakeBackend backend;
  int sent_count = 0;

  const auto outcome = sender::send_packet(
    backend, kTestPacketBytes, nullptr, [](std::uint8_t* dst) { dst[0] = 0xab; }, [&]() { ++sent_count; });

  BOOST_CHECK(outcome == sender::TxOutcome::kSent);
  BOOST_CHECK_EQUAL(sent_count, 1);
  BOOST_CHECK_EQUAL(backend.tx_calls, 1);
  BOOST_CHECK_EQUAL(backend.free_calls, 0); // NIC owns the buffer
  BOOST_CHECK_EQUAL(backend.storage[0], 0xab);
}

BOOST_AUTO_TEST_CASE(EngineBackpressureIsOneAttemptAndFreesExactlyOnce)
{
  FakeBackend backend;
  backend.fail_tx = true;
  bool sent = false;

  const auto outcome = sender::send_packet(
    backend, kTestPacketBytes, nullptr, [](std::uint8_t*) {}, [&]() { sent = true; });

  BOOST_CHECK(outcome == sender::TxOutcome::kTxFailed);
  BOOST_CHECK(!sent);
  BOOST_CHECK_EQUAL(backend.tx_calls, 1); // refused once, recorded, not retried
  BOOST_CHECK_EQUAL(backend.free_calls, 1);
}

BOOST_AUTO_TEST_CASE(EnginePreTxCopyCapturesConstructedBytes)
{
  FakeBackend backend;
  backend.fail_tx = true; // even on failure the copy must exist
  std::array<std::uint8_t, kTestPacketBytes> copy{};

  const auto outcome = sender::send_packet(
    backend, kTestPacketBytes, copy.data(),
    [](std::uint8_t* dst) {
      for (std::size_t i = 0; i < kTestPacketBytes; ++i) {
        dst[i] = static_cast<std::uint8_t>(i + 1);
      }
    },
    []() {});

  BOOST_CHECK(outcome == sender::TxOutcome::kTxFailed);
  for (std::size_t i = 0; i < kTestPacketBytes; ++i) {
    BOOST_CHECK_EQUAL(copy[i], static_cast<std::uint8_t>(i + 1));
  }
}

// ---------------------------------------------------------------------------
// CallbackGate lifetime contract (F09)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(GateDispatchesOnlyWhenArmed)
{
  sender::CallbackGate<int> gate;
  int owner = 42;

  int calls = 0;
  BOOST_CHECK(!gate.dispatch([&](int&) { ++calls; })); // unarmed
  BOOST_CHECK_EQUAL(calls, 0);

  gate.arm(&owner);
  BOOST_CHECK(gate.armed());
  BOOST_CHECK(gate.dispatch([&](int& value) {
    ++calls;
    BOOST_CHECK_EQUAL(value, 42);
  }));
  BOOST_CHECK_EQUAL(calls, 1);

  gate.tombstone();
  BOOST_CHECK(!gate.armed());
  BOOST_CHECK(!gate.dispatch([&](int&) { ++calls; }));
  BOOST_CHECK_EQUAL(calls, 1);

  // Re-arm (configure after scrap on the same instance) restores dispatch.
  gate.arm(&owner);
  BOOST_CHECK(gate.dispatch([&](int&) { ++calls; }));
  BOOST_CHECK_EQUAL(calls, 2);
}

BOOST_AUTO_TEST_CASE(GateTombstoneDrainsInFlightDispatch)
{
  sender::CallbackGate<int> gate;
  int owner = 7;
  gate.arm(&owner);

  // Bounded coordination: the dispatch signals entry, then blocks on a release
  // future.  A second thread calls tombstone(), which must not return while the
  // dispatch is inside the gate.
  std::promise<void> entered_promise;
  auto entered = entered_promise.get_future();
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();

  std::atomic<bool> dispatch_finished{ false };
  std::atomic<bool> tombstone_returned{ false };

  std::thread worker([&]() {
    gate.dispatch([&](int&) {
      entered_promise.set_value();
      release.wait();
      dispatch_finished.store(true);
    });
  });

  BOOST_REQUIRE(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready);

  std::thread tombstoner([&]() {
    gate.tombstone();
    tombstone_returned.store(true);
  });

  // Allow the second thread time to reach the writer lock.  It must remain
  // blocked while the dispatch is held open.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  BOOST_CHECK(!tombstone_returned.load());

  // Release the dispatch.  tombstone() must then complete.
  release_promise.set_value();
  worker.join();
  tombstoner.join();

  BOOST_CHECK(dispatch_finished.load());
  BOOST_CHECK(tombstone_returned.load());
  BOOST_CHECK(!gate.armed());
}

// ---------------------------------------------------------------------------
// WIBEthTransmitter core: registry callbacks, accounting, observer hook
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TransmitterRegistryPathBuildsAndSends)
{
  test_transmitter_t tx;
  tx.add_stream("stream-a", 0);
  tx.add_stream("stream-b", 100);
  tx.configure(wibeth::PacketConfig{}, 3, FakeTxBackend{});

  FakeCallbackRegistry registry;
  FakeCallbackConf conf_a{ "stream-a" };
  FakeCallbackConf conf_b{ "stream-b" };
  const std::vector<const FakeCallbackConf*> confs{ &conf_a, &conf_b };
  tx.register_streams<FakePayload>(registry, confs);
  BOOST_REQUIRE_EQUAL(registry.callbacks.size(), 2);
  BOOST_CHECK(tx.armed());

  tx.set_accepting(true);
  FakePayload payload;
  for (std::size_t i = 0; i < sizeof(payload.data); ++i) {
    payload.data[i] = static_cast<char>(i % 251);
  }
  registry.callbacks.at("stream-a")(FakePayload(payload));

  const auto stats = tx.snapshot_stats();
  BOOST_CHECK_EQUAL(stats.at("stream-a").accepted, 1);
  BOOST_CHECK_EQUAL(stats.at("stream-a").sent, 1);
  BOOST_CHECK_EQUAL(stats.at("stream-a").next_seq, 1);
  BOOST_CHECK_EQUAL(stats.at("stream-b").accepted, 0);
  BOOST_CHECK_EQUAL(tx.backend().tx_calls, 1);
  BOOST_CHECK_EQUAL(tx.backend().free_calls, 0); // NIC owns the buffer

  // Payload bytes appear unchanged after the patched DAQEthHeader.  The patch
  // carries det_id and the stream sequence number.
  const auto& packet = tx.backend().storage;
  BOOST_CHECK(std::memcmp(packet.data() + wibeth::kPacketHeaderBytes + wibeth::kDAQEthHeaderBytes,
                          payload.data + wibeth::kDAQEthHeaderBytes,
                          wibeth::kWIBEthFrameBytes - wibeth::kDAQEthHeaderBytes) == 0);
  const auto header = read_wire_daq_header(packet);
  BOOST_CHECK_EQUAL(header.det_id, 3);
  BOOST_CHECK_EQUAL(header.seq_id, 0);
}

BOOST_AUTO_TEST_CASE(TransmitterUnknownStreamIsCountedDrop)
{
  test_transmitter_t tx;
  tx.add_stream("known", 0);
  tx.configure(wibeth::PacketConfig{}, 0, FakeTxBackend{});
  tx.set_accepting(true);

  tx.transmit("mystery", FakePayload{});

  BOOST_CHECK_EQUAL(tx.unknown_stream_drops(), 1);
  BOOST_CHECK_EQUAL(tx.snapshot_stats().at("known").accepted, 0);
  BOOST_CHECK_EQUAL(tx.backend().tx_calls, 0);
}

BOOST_AUTO_TEST_CASE(TransmitterDropsWhileNotAccepting)
{
  test_transmitter_t tx;
  tx.add_stream("stream-a", 0);
  tx.configure(wibeth::PacketConfig{}, 0, FakeTxBackend{});

  FakeCallbackRegistry registry;
  FakeCallbackConf conf_a{ "stream-a" };
  tx.register_streams<FakePayload>(registry, std::vector<const FakeCallbackConf*>{ &conf_a });

  // accepting defaults to false (pre-start / post-stop)
  registry.callbacks.at("stream-a")(FakePayload{});

  const auto stats = tx.snapshot_stats();
  BOOST_CHECK_EQUAL(stats.at("stream-a").accepted, 1);
  BOOST_CHECK_EQUAL(stats.at("stream-a").dropped_not_running, 1);
  BOOST_CHECK_EQUAL(stats.at("stream-a").sent, 0);
  BOOST_CHECK_EQUAL(tx.backend().tx_calls, 0);
}

BOOST_AUTO_TEST_CASE(TransmitterSequenceWrapsModulo4096)
{
  test_transmitter_t tx;
  tx.add_stream("stream-a", 4095);
  tx.configure(wibeth::PacketConfig{}, 0, FakeTxBackend{});
  tx.set_accepting(true);

  tx.transmit("stream-a", FakePayload{});
  BOOST_CHECK_EQUAL(read_wire_daq_header(tx.backend().storage).seq_id, 4095);

  tx.transmit("stream-a", FakePayload{});
  BOOST_CHECK_EQUAL(read_wire_daq_header(tx.backend().storage).seq_id, 0);

  const auto stats = tx.snapshot_stats();
  BOOST_CHECK_EQUAL(stats.at("stream-a").sent, 2);
  BOOST_CHECK_EQUAL(stats.at("stream-a").next_seq, 1);
}

BOOST_AUTO_TEST_CASE(TransmitterTombstoneDrainsRegistryLambdaToSafeDrop)
{
  test_transmitter_t tx;
  tx.add_stream("stream-a", 0);
  tx.configure(wibeth::PacketConfig{}, 0, FakeTxBackend{});

  FakeCallbackRegistry registry;
  FakeCallbackConf conf_a{ "stream-a" };
  tx.register_streams<FakePayload>(registry, std::vector<const FakeCallbackConf*>{ &conf_a });
  tx.set_accepting(true);

  registry.callbacks.at("stream-a")(FakePayload{});
  BOOST_CHECK_EQUAL(tx.snapshot_stats().at("stream-a").sent, 1);

  tx.tombstone();
  BOOST_CHECK(!tx.armed());

  // The registry still holds the lambda.  Dispatch becomes a drop: no
  // accounting and no backend call.
  registry.callbacks.at("stream-a")(FakePayload{});
  BOOST_CHECK_EQUAL(tx.snapshot_stats().at("stream-a").accepted, 1);
  BOOST_CHECK_EQUAL(tx.backend().tx_calls, 1);
}

BOOST_AUTO_TEST_CASE(TransmitterReRegisterReArmsWithoutDuplicateRegistration)
{
  test_transmitter_t tx;
  tx.add_stream("stream-a", 0);
  tx.configure(wibeth::PacketConfig{}, 0, FakeTxBackend{});

  FakeCallbackRegistry registry;
  FakeCallbackConf conf_a{ "stream-a" };
  const std::vector<const FakeCallbackConf*> confs{ &conf_a };
  tx.register_streams<FakePayload>(registry, confs);
  tx.tombstone();

  // configure after scrap on the same instance: re-arm only.  The pinned
  // registry would reject a second registration, so none is attempted.
  tx.register_streams<FakePayload>(registry, confs);
  BOOST_CHECK_EQUAL(registry.callbacks.size(), 1);
  BOOST_CHECK_EQUAL(registry.rejected_duplicates, 0);
  BOOST_CHECK(tx.armed());

  tx.set_accepting(true);
  registry.callbacks.at("stream-a")(FakePayload{});
  BOOST_CHECK_EQUAL(tx.snapshot_stats().at("stream-a").sent, 1);
}

BOOST_AUTO_TEST_CASE(TransmitterObserverSeesPostTxTruthOnSuccessOnly)
{
  test_transmitter_t tx;
  tx.add_stream("stream-a", 0);
  tx.configure(wibeth::PacketConfig{}, 0, FakeTxBackend{});
  tx.set_accepting(true);

  int observer_calls = 0;
  std::vector<std::uint8_t> observed;
  tx.set_post_tx_observer([&](const std::uint8_t* bytes, std::size_t len) {
    ++observer_calls;
    observed.assign(bytes, bytes + len);
  });

  tx.transmit("stream-a", FakePayload{});
  BOOST_REQUIRE_EQUAL(observer_calls, 1);
  BOOST_REQUIRE_EQUAL(observed.size(), wibeth::kEthernetPacketBytes);
  BOOST_CHECK(std::memcmp(observed.data(), tx.backend().storage.data(), wibeth::kEthernetPacketBytes) == 0);

  // Failed transmits are not observed.
  tx.backend().fail_transmit = true;
  tx.transmit("stream-a", FakePayload{});
  BOOST_CHECK_EQUAL(observer_calls, 1);
  BOOST_CHECK_EQUAL(tx.snapshot_stats().at("stream-a").tx_failures, 1);
}

BOOST_AUTO_TEST_SUITE_END()
