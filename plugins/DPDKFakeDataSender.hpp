/**
 * @file DPDKFakeDataSender.hpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 *
 * Thin DAQModule shell for the DPDK WIBEth fake data sender.  DAL loading,
 * pre-EAL configuration guards, port/queue bring-up, FSM gating, and
 * idempotent teardown/rollback live in sender::FakeSenderCore, instantiated
 * here with sender::DpdkPortRuntime and sender::DpdkTxBackend.  The complete
 * transmission callback (per-stream sequencing/accounting, registry
 * instruments, callback-lifetime gating) lives in sender::WIBEthTransmitter.
 *
 * One transmitter, TX queue and mempool per stream: stream i uses TX queue i.
 * Each transmitter mutex, callback gate, TX queue and mempool is used only by
 * that stream's producer thread.
 *
 * Addresses: the module's detector-to-DAQ connection (the objects the
 * receiver reads) is the only source.  Destination MAC and IP are those of the
 * net_receiver interface.  A stream's source IP is the first ip_address of
 * the sender in net_senders that lists the stream's source_id; its detector
 * ID is detector_id of its GeoId.
 *
 * Lifecycle and ownership contract:
 *  - Registered callbacks never capture this module or a transmitter
 *    directly; they capture the transmitter's CallbackGate, armed on
 *    configure and tombstoned (with in-flight drain) on scrap/destruction.
 *    Because the pinned callback registry has no unregister operation,
 *    destroying this module and creating a replacement with the same callback
 *    ID in one process is unsupported; conf -> scrap -> conf on the same
 *    instance is the supported cycle.
 *  - Each TX mempool is held in a unique_mempool (rte_mempool_free deleter)
 *    and freed before EAL cleanup, which also releases the pool name for
 *    re-configuration.
 *  - All transmit failure paths (allocation, tailroom, TX backpressure) are
 *    contained: they count, they never throw out of the producer callback,
 *    and they never leak an mbuf.
 */
#ifndef DPDKLIBS_PLUGINS_DPDKFAKEDATASENDER_HPP_
#define DPDKLIBS_PLUGINS_DPDKFAKEDATASENDER_HPP_

#include "appfwk/DAQModule.hpp"
#include "appmodel/DataMoveCallbackConf.hpp"
#include "dpdklibs/sender/DpdkPortRuntime.hpp"
#include "dpdklibs/sender/DpdkTxBackend.hpp"
#include "dpdklibs/sender/FakeSenderCore.hpp"
#include "fdreadoutlibs/DUNEWIBEthTypeAdapter.hpp"

#include <memory>
#include <string>

namespace dunedaq::dpdklibs {

class DPDKFakeDataSender : public dunedaq::appfwk::DAQModule
{
public:
  explicit DPDKFakeDataSender(const std::string& name);
  ~DPDKFakeDataSender() override;

  DPDKFakeDataSender(const DPDKFakeDataSender&) = delete;
  DPDKFakeDataSender& operator=(const DPDKFakeDataSender&) = delete;
  DPDKFakeDataSender(DPDKFakeDataSender&&) = delete;
  DPDKFakeDataSender& operator=(DPDKFakeDataSender&&) = delete;

  void init(const std::shared_ptr<appfwk::ConfigurationManager> cfg) override;

private:
  using wibeth_adapter_t = fdreadoutlibs::types::DUNEWIBEthTypeAdapter;
  using core_t =
    sender::FakeSenderCore<sender::DpdkPortRuntime, sender::DpdkTxBackend, wibeth_adapter_t, appmodel::DataMoveCallbackConf>;

  void do_configure(const CommandData_t& args);
  void do_start(const CommandData_t& args);
  void do_enable_triggers(const CommandData_t& args);
  void do_stop(const CommandData_t& args);
  void do_scrap(const CommandData_t& args);

  // Declared before m_core so m_core tears down while DAL objects in m_cfg are still alive.
  std::shared_ptr<appfwk::ConfigurationManager> m_cfg;
  core_t m_core;
};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_PLUGINS_DPDKFAKEDATASENDER_HPP_
