/**
 * @file CallbackGate.hpp
 *
 * Callback-lifetime tombstone for process-global callback registries that have
 * no unregister or replace operation.  The pinned datahandlinglibs
 * DataMoveCallbackRegistry retains the first registration for an ID for the
 * lifetime of the process.
 *
 * A registered lambda captures a shared_ptr<CallbackGate<Owner>> rather than a
 * raw owner pointer.  The shared_ptr keeps the gate alive for as long as the
 * registry holds the lambda.  The owning module controls dispatch:
 *
 *  - arm(owner): enable dispatch (configure or re-configure).
 *  - tombstone(): acquire the writer lock, which drains every in-flight
 *    dispatch, then disarm.  After it returns, no dispatch can reach the owner.
 *    Call it before releasing the owner or its DPDK resources.
 *
 * One-shot contract: destroying a module and creating a replacement with the
 * same callback ID in one process is not supported.  The registry would retain
 * the first, permanently tombstoned lambda and drop the data.  Re-arming the
 * same gate on the same module instance (configure after scrap) is supported
 * and is the intended lifecycle.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_CALLBACKGATE_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_CALLBACKGATE_HPP_

#include <mutex>
#include <shared_mutex>
#include <utility>

namespace dunedaq::dpdklibs::sender {

template<typename Owner>
class CallbackGate
{
public:
  CallbackGate() = default;
  CallbackGate(const CallbackGate&) = delete;
  CallbackGate& operator=(const CallbackGate&) = delete;
  CallbackGate(CallbackGate&&) = delete;
  CallbackGate& operator=(CallbackGate&&) = delete;

  void arm(Owner* owner)
  {
    std::unique_lock lock(m_mutex);
    m_owner = owner;
  }

  // Blocks until every in-flight dispatch() has returned, then disarms.
  void tombstone()
  {
    std::unique_lock lock(m_mutex);
    m_owner = nullptr;
  }

  bool armed() const
  {
    std::shared_lock lock(m_mutex);
    return m_owner != nullptr;
  }

  // Invoke fn(*owner) under a shared lock if armed.  Returns true if fn ran.
  // The shared lock guarantees the owner outlives the call with respect to
  // tombstone().  It does not serialize concurrent dispatches.
  template<typename Fn>
  bool dispatch(Fn&& fn)
  {
    std::shared_lock lock(m_mutex);
    if (m_owner == nullptr) {
      return false;
    }
    std::forward<Fn>(fn)(*m_owner);
    return true;
  }

private:
  mutable std::shared_mutex m_mutex;
  Owner* m_owner = nullptr;
};

} // namespace dunedaq::dpdklibs::sender

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_CALLBACKGATE_HPP_
