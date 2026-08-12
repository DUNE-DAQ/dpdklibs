/**
 * @file TxEngine.hpp
 *
 * Single-packet transmit with RAII buffer ownership.  The engine is templated
 * on a Backend, so the failure matrix (allocation failure, tailroom failure,
 * TX backpressure) is unit-testable without EAL or a NIC.
 *
 * Backend concept:
 *   void*         alloc();                              // nullptr on failure
 *   std::uint8_t* prepare(void* buf, std::size_t len);  // bounded append; nullptr on failure
 *   bool          transmit(void* buf);                  // true = NIC took ownership
 *   void          free(void* buf);
 *
 * One transmit attempt per packet.  A full TX ring yields kTxFailed, which the
 * caller counts; there is no retry loop.  At the rates measured for this sender
 * the TX ring drains faster than it fills and the former retry path never
 * executed.  At rates where backpressure is expected, the correct mechanism is
 * a dedicated TX queue owner, not blocking inside a producer callback.
 *
 * Sequence numbers advance in on_sent() only.  A drop therefore produces no
 * sequence gap on the wire; it appears downstream as a missing timestamp.
 *
 * Ownership rule: on kSent the backend owns the buffer.  On every other
 * outcome the engine frees it exactly once through BufGuard.  No path leaks and
 * no path double-frees.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_TXENGINE_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_TXENGINE_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dunedaq::dpdklibs::sender {

enum class TxOutcome
{
  kSent,          // backend took ownership of the buffer
  kAllocFailed,   // no buffer was available
  kPrepareFailed, // append rejected: insufficient tailroom
  kTxFailed       // backend refused the packet; buffer freed
};

template<typename Backend>
class BufGuard
{
public:
  BufGuard(Backend& backend, void* buf)
    : m_backend(backend)
    , m_buf(buf)
  {
  }
  ~BufGuard()
  {
    if (m_buf != nullptr) {
      m_backend.free(m_buf);
    }
  }
  BufGuard(const BufGuard&) = delete;
  BufGuard& operator=(const BufGuard&) = delete;
  BufGuard(BufGuard&&) = delete;
  BufGuard& operator=(BufGuard&&) = delete;

  void release() { m_buf = nullptr; }

private:
  Backend& m_backend;
  void* m_buf;
};

// Allocate, build, and transmit one packet.
//
//  - build(dst) writes exactly packet_bytes into dst.
//  - If pre_tx_copy is non-null, the constructed bytes are copied there before
//    the transmit attempt.  The buffer is not readable after a successful
//    transmit, so this copy is the only record of the transmitted bytes.
//  - on_sent() runs only after a successful transmit.
template<typename Backend, typename BuildFn, typename OnSentFn>
TxOutcome
send_packet(Backend& backend,
            std::size_t packet_bytes,
            std::uint8_t* pre_tx_copy,
            BuildFn&& build,
            OnSentFn&& on_sent)
{
  void* buf = backend.alloc();
  if (buf == nullptr) {
    return TxOutcome::kAllocFailed;
  }
  BufGuard<Backend> guard(backend, buf);

  std::uint8_t* dst = backend.prepare(buf, packet_bytes);
  if (dst == nullptr) {
    return TxOutcome::kPrepareFailed;
  }

  build(dst);
  if (pre_tx_copy != nullptr) {
    std::memcpy(pre_tx_copy, dst, packet_bytes);
  }

  if (!backend.transmit(buf)) {
    return TxOutcome::kTxFailed;
  }

  guard.release();
  on_sent();
  return TxOutcome::kSent;
}

} // namespace dunedaq::dpdklibs::sender

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_TXENGINE_HPP_
