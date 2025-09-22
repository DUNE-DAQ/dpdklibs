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

#include "dpdklibs/Issues.hpp"

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "dpdklibs/opmon/SourceModel.pb.h"

#include "datahandlinglibs/DataMoveCallbackRegistry.hpp"

// #include <folly/ProducerConsumerQueue.h>
// #include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>


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
  {}
  ~SourceModel() {}

  void set_sink(const std::string& sink_name, bool callback_mode) override
  {
    m_callback_mode = callback_mode;
    if (callback_mode) {
      TLOG_DEBUG(5) << "Callback mode requested. Won't acquire iom sender!";
    } else {
      if (m_sink_is_set) {
        TLOG_DEBUG(5) << "SourceModel sink is already set in initialized!";
      } else {
        m_sink_queue = get_iom_sender<TargetPayloadType>(sink_name);
        m_sink_is_set = true;
      }
    }
  }

  void acquire_callback() override
  {
    if (m_callback_mode) {
      if (m_callback_is_acquired) {
        TLOG_DEBUG(5) << "SourceModel callback is already acquired!";
      } else {
        // Getting DataMoveCBRegistry
        auto dmcbr = datahandlinglibs::DataMoveCallbackRegistry::get();
        m_sink_callback = dmcbr->get_callback<TargetPayloadType>(inherited::m_sink_name);
        m_callback_is_acquired = true;
      }
    } else {
      TLOG_DEBUG(5) << "Won't acquire callback, as IOM sink is set!";
    }
  }

  // Exposes sink via returning a pointer to it. 
  std::shared_ptr<sink_t>& get_sink() { return m_sink_queue; }

  // Process an incoming raw byte buffer and extract complete frames of type TargetPayloadType.
  void handle_daq_frame(char* buffer, std::size_t size)
  {
    // Calculate how many full frames fit in the incoming message buffer.
    std::size_t full_frames = size / m_expected_frame_size;
    
    // Calculate leftover bytes that don't form a complete frame.
    if (size % m_expected_frame_size > 0) [[unlikely]] {
      ++m_leftover_bytes_encountered;
    }
    
    // Process each full frames
    for (std::size_t i = 0; i < full_frames; ++i) {
      // Calculate pointer to the i-th frame chunk inside the message buffer.
      const char* src = buffer + i * m_expected_frame_size;
    
      // Materialize a real TargetPayloadType object by copying bytes from the buffer.
      // This is defined behavior, alignment-safe, and fast, without pointer vodoo
      // Previously reinterpret_cast to TargetPayloadType* introduced alignment traps 
      // “pretend there’s a constructed object there” UB. Scatter won't work like that.
      TargetPayloadType frame;
      std::memcpy(&frame, src, m_expected_frame_size);

      if (m_callback_mode) {
        // Pass by value (moved); no references into 'buffer', so no UAF.
        (*m_sink_callback)(std::move(frame));
      } else {
        // Queue mode: attempt to enqueue the frame in a non-blocking way.
        if (!m_sink_queue->try_send(std::move(frame), iomanager::Sender::s_no_block)) {
           ++m_failed_to_send_daq_payloads;
        }
      }
    }
  }

  void generate_opmon_data() override {
      
    if(m_failed_to_send_daq_payloads != 0) {
        ers::warning(FailedToSendData(ERS_HERE, m_sink_id, m_failed_to_send_daq_payloads));
    }

    opmon::SourceInfo info;
    info.set_failed_to_send_daq_payloads( m_failed_to_send_daq_payloads.exchange(0) );
    info.set_leftover_bytes_encountered( m_leftover_bytes_encountered.exchange(0) );

    publish( std::move(info) );
  }
  
private:
  // Constants
  const std::size_t m_expected_frame_size = sizeof(TargetPayloadType);

  // Sink internals
  std::string m_sink_id;
  bool m_sink_is_set{ false };
  std::shared_ptr<sink_t> m_sink_queue;

  // Callback internals
  bool m_callback_mode;
  bool m_callback_is_acquired{ false };
  using sink_cb_t = std::shared_ptr<std::function<void(TargetPayloadType&&)>>;
  sink_cb_t m_sink_callback;

  // Stats
  std::atomic<uint64_t> m_leftover_bytes_encountered{0};
  std::atomic<uint64_t> m_failed_to_send_daq_payloads{0};

};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_SRC_SOURCEMODEL_HPP_
