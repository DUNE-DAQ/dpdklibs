/**
 * @file WIBEthTransmitter.hpp
 *
 * WIBEth transmitter core: per-stream sequencing and accounting,
 * DataMoveCallbackRegistry callback registration, callback-lifetime gating, and
 * the post-transmit observer hook.  Templated on the TX Backend (TxEngine.hpp
 * concept) and, at the registration call site, on the registry and
 * configuration-pointer types.  The complete transmission callback is
 * therefore unit-testable without EAL, a NIC, or an OKS database, while the
 * plugin instantiates it against the datahandlinglibs registry.
 *
 * Lifecycle contract (see CallbackGate.hpp):
 *  - register_streams() registers one gate-capturing lambda per stream, at most
 *    once per process, then arms the gate.  The pinned registry has no
 *    unregister or replace operation.  A later call after a tombstone
 *    (configure after scrap on the same instance) only re-arms.
 *  - tombstone() drains every in-flight dispatch and blocks new ones.  Call it
 *    before releasing any resource the Backend uses.  The destructor
 *    tombstones, so a registry that outlives this object dispatches to a drop:
 *    the lambdas capture the gate, not the transmitter.
 *
 * Payload contract: PayloadT provides a `data` member convertible to
 * const void* addressing at least one complete WIBEthFrame.
 * fdreadoutlibs::types::DUNEWIBEthTypeAdapter satisfies this.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_WIBETHTRANSMITTER_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_WIBETHTRANSMITTER_HPP_

#include "dpdklibs/sender/CallbackGate.hpp"
#include "dpdklibs/sender/TxEngine.hpp"
#include "dpdklibs/wibeth/WIBEthPacketBuilder.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace dunedaq::dpdklibs::sender {

template<typename Backend>
class WIBEthTransmitter
{
public:
  // Called with the transmitted bytes after each successful transmit; packets
  // the backend rejected are not observed.  Runs under the transmitter mutex.
  // Must not throw.  Its execution time adds to the transmit path.
  using post_tx_observer_t = std::function<void(const std::uint8_t*, std::size_t)>;

  struct StreamStats
  {
    std::uint64_t next_seq = 0;
    std::uint64_t accepted = 0;
    std::uint64_t sent = 0;
    std::uint64_t dropped_not_running = 0;
    std::uint64_t tx_alloc_failures = 0;
    std::uint64_t tx_prepare_failures = 0;
    std::uint64_t tx_failures = 0;
  };

  WIBEthTransmitter() = default;
  ~WIBEthTransmitter() { m_gate->tombstone(); }

  WIBEthTransmitter(const WIBEthTransmitter&) = delete;
  WIBEthTransmitter& operator=(const WIBEthTransmitter&) = delete;
  WIBEthTransmitter(WIBEthTransmitter&&) = delete;
  WIBEthTransmitter& operator=(WIBEthTransmitter&&) = delete;

  // Create per-stream state.  Idempotent for an existing id: a repeated call
  // does not reset live counters.
  void add_stream(const std::string& callback_id, std::uint64_t initial_seq_id)
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    StreamStats stats;
    stats.next_seq = initial_seq_id % 4096;
    m_streams.emplace(callback_id, stats);
  }

  void configure(const wibeth::PacketConfig& packet_config, std::uint64_t det_id, const Backend& backend)
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_packet_config = packet_config;
    m_det_id = det_id;
    m_backend = backend;
  }

  // An empty observer disables the pre-transmit byte copy.
  void set_post_tx_observer(post_tx_observer_t observer)
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_post_tx_observer = std::move(observer);
  }

  void set_accepting(bool accepting) { m_accepting.store(accepting); }
  bool accepting() const { return m_accepting.load(); }

  // Drains in-flight dispatches, then blocks new ones until the next
  // register_streams() re-arms the gate.
  void tombstone() { m_gate->tombstone(); }
  bool armed() const { return m_gate->armed(); }

  // Registers one gate-dispatching lambda per configuration, then arms the
  // gate.  Registry contract (matched by the pinned
  // datahandlinglibs::DataMoveCallbackRegistry):
  //   registry.register_callback<PayloadT>(conf, std::function<void(PayloadT&&)>)
  // keyed by conf->UID().
  template<typename PayloadT, typename Registry, typename ConfPtr>
  void register_streams(Registry& registry, const std::vector<ConfPtr>& callback_confs)
  {
    if (m_streams_registered) {
      m_gate->arm(this);
      return;
    }
    for (const auto& conf : callback_confs) {
      const std::string callback_id = conf->UID();
      // The lambda captures the gate, not the transmitter.  If the
      // process-global registry outlives this object, dispatch becomes a drop
      // rather than a call through a dangling pointer.
      auto gate = m_gate;
      registry.template register_callback<PayloadT>(
        conf, [gate, callback_id](PayloadT&& payload) {
          gate->dispatch(
            [&](WIBEthTransmitter& owner) { owner.transmit(callback_id, std::move(payload)); });
        });
    }
    m_streams_registered = true;
    m_gate->arm(this);
  }

  template<typename PayloadT>
  void transmit(const std::string& callback_id, PayloadT&& payload)
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto state_it = m_streams.find(callback_id);
    if (state_it == m_streams.end()) {
      ++m_unknown_stream_drops;
      return;
    }
    auto& state = state_it->second;
    ++state.accepted;

    if (!m_accepting.load()) {
      ++state.dropped_not_running;
      return;
    }

    auto packet_config = m_packet_config;
    packet_config.packet_id = static_cast<std::uint16_t>(state.sent & 0xffffU);

    wibeth::HeaderPatch patch;
    patch.set_det_id = true;
    patch.det_id = m_det_id;
    patch.set_seq_id = true;
    patch.seq_id = state.next_seq;
    patch.force_block_length = true;

    const bool want_observer = static_cast<bool>(m_post_tx_observer);
    std::uint8_t* pre_tx_copy = want_observer ? m_observer_scratch.data() : nullptr;

    const auto outcome = send_packet(
      m_backend,
      wibeth::kEthernetPacketBytes,
      pre_tx_copy,
      [&](std::uint8_t* dst) { wibeth::construct_packet(dst, payload.data, packet_config, patch); },
      [&]() {
        ++state.sent;
        state.next_seq = (state.next_seq + 1) % 4096;
      });

    switch (outcome) {
      case TxOutcome::kSent:
        if (want_observer) {
          m_post_tx_observer(m_observer_scratch.data(), wibeth::kEthernetPacketBytes);
        }
        break;
      case TxOutcome::kAllocFailed:
        ++state.tx_alloc_failures;
        break;
      case TxOutcome::kPrepareFailed:
        ++state.tx_prepare_failures;
        break;
      case TxOutcome::kTxFailed:
        ++state.tx_failures;
        break;
    }
  }

  std::map<std::string, StreamStats> snapshot_stats() const
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_streams;
  }

  std::uint64_t unknown_stream_drops() const { return m_unknown_stream_drops.load(); }

  // Configuration and inspection access.  Not synchronized against in-flight
  // transmits; call from the owning module's command context or from tests.
  Backend& backend() { return m_backend; }

private:
  mutable std::mutex m_mutex;
  std::map<std::string, StreamStats> m_streams;

  std::shared_ptr<CallbackGate<WIBEthTransmitter>> m_gate =
    std::make_shared<CallbackGate<WIBEthTransmitter>>();
  bool m_streams_registered = false;

  std::atomic<bool> m_accepting{ false };
  std::atomic<std::uint64_t> m_unknown_stream_drops{ 0 };

  Backend m_backend{};
  wibeth::PacketConfig m_packet_config{};
  std::uint64_t m_det_id = 0;

  post_tx_observer_t m_post_tx_observer;
  // Destination of the pre-transmit byte copy.  Guarded by m_mutex, like the
  // rest of the transmit path.
  std::array<std::uint8_t, wibeth::kEthernetPacketBytes> m_observer_scratch{};
};

} // namespace dunedaq::dpdklibs::sender

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_WIBETHTRANSMITTER_HPP_
