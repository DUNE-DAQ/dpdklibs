/**
 * @file SourceModel.hpp FELIX CR's ELink concept wrapper
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_SOURCEMODEL_HPP_
#define DPDKLIBS_SRC_SOURCEMODEL_HPP_

#include "SourceConcept.hpp"

//#include "packetformat/block_format.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"
// #include "readoutlibs/utils/ReusableThread.hpp"

#include "readoutlibs/DataMoveCallbackRegistry.hpp"

// #include <folly/ProducerConsumerQueue.h>
// #include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "dpdklibs/nicreaderinfo/InfoNljs.hpp"


namespace dunedaq::dpdklibs {

template<class TargetPayloadType>
class SourceModel : public SourceConcept
{
public:
  using sink_t = iomanager::SenderConcept<TargetPayloadType>;
  using inherited = SourceConcept;
  using data_t = nlohmann::json;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  SourceModel()
    : SourceConcept()
    , m_run_marker{ false }
    //, m_parser_thread(0)
  {}
  ~SourceModel() {}

  void set_sink(const std::string& sink_name) override
  {
    if (m_sink_is_set) {
      TLOG_DEBUG(5) << "SourceModel sink is already set in initialized!";
    } else {
      m_sink_queue = get_iom_sender<TargetPayloadType>(sink_name);
      m_sink_is_set = true;
    }
  }

  void acquire_callback() override
  {
    if (m_callback_is_acquired) {
      TLOG_DEBUG(5) << "SourceModel callback is already acquired!";
    } else {
      // Getting DataMoveCBRegistry
      auto dmcbr = readoutlibs::DataMoveCallbackRegistry::get();
      m_sink_callback = dmcbr->get_callback<TargetPayloadType>(inherited::m_sink_name);
      m_callback_is_acquired = true;
    }
  }

  std::shared_ptr<sink_t>& get_sink() { return m_sink_queue; }

  //std::shared_ptr<err_sink_t>& get_error_sink() { return m_error_sink_queue; }

  void init(const data_t& /*args*/)
  {
  }

  void conf(const data_t& /*args*/)
  {
    if (m_configured) {
      TLOG_DEBUG(5) << "SourceModel is already configured!";
    } else {
      m_configured = true;
    }
  }

  void start(const data_t& /*args*/)
  {
    m_dropped_packets = { 0 };

    m_t0 = std::chrono::high_resolution_clock::now();
    if (!m_run_marker.load()) {
      set_running(true);
      TLOG_DEBUG(5) << "Started SourceModel of link " << inherited::m_opmon_str << "...";
    } else {
      TLOG_DEBUG(5) << "SourceModel of link " << inherited::m_opmon_str << " is already running!";
    }
  }

  void stop(const data_t& /*args*/)
  {
    if (m_run_marker.load()) {
      set_running(false);
      //}
      TLOG_DEBUG(5) << "Stopped SourceModel of link " << m_opmon_str << "!";
    } else {
      TLOG_DEBUG(5) << "SourceModel of link " << m_opmon_str << " is already stopped!";
    }
  }

  void set_running(bool should_run)
  {
    bool was_running = m_run_marker.exchange(should_run);
    TLOG_DEBUG(5) << "Active state was toggled from " << was_running << " to " << should_run;
  }


  bool handle_payload(char* message, std::size_t size) // NOLINT(build/unsigned)
  {
    bool push_out = true;
    if (push_out) {

////////////////////////////////////
// RS FIXME: Non optional callbacks. This version only works with callbacks setup.
      TargetPayloadType& target_payload = *reinterpret_cast<TargetPayloadType*>(message);
      (*m_sink_callback)(std::move(target_payload));
/*
      if (!m_sink_queue->try_send(std::move(target_payload), iomanager::Sender::s_no_block)) {
        //if(m_dropped_packets == 0 || m_dropped_packets%10000) {
        //  TLOG() << "Dropped data " << m_dropped_packets;
        //}
        ++m_dropped_packets;
      }
*/
///////////////////////////////////

    } else {
      TargetPayloadType target_payload;
      uint32_t bytes_copied = 0;
      readoutlibs::buffer_copy(message, size, static_cast<void*>(&target_payload), bytes_copied, sizeof(target_payload));
    }


    return true;
  }

  void get_info(opmonlib::InfoCollector& ci, int /*level*/) {
    nicreaderinfo::SourceStats ss;
    ss.dropped_frames = m_dropped_packets.load();
    ci.add(ss);
  }

private:
  // Internals
  std::atomic<bool> m_run_marker;
  bool m_configured{ false };

  std::string m_sink_id;

  // Sink
  bool m_sink_is_set{ false };
  std::shared_ptr<sink_t> m_sink_queue;
  //std::shared_ptr<err_sink_t> m_error_sink_queue;

  // Callback
  bool m_callback_is_acquired{ false };
  using sink_cb_t = std::shared_ptr<std::function<void(TargetPayloadType&&)>>;
  sink_cb_t m_sink_callback;

  std::atomic<uint64_t> m_dropped_packets{0};

};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_SRC_SOURCEMODEL_HPP_
