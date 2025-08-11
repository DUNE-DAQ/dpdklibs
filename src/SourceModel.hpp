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

  std::shared_ptr<sink_t>& get_sink() { return m_sink_queue; }

  // Process an incoming raw byte buffer and extract complete payloads of type TargetPayloadType.
  bool handle_payload(char* message, std::size_t size)
  {
      // Determine the exact size in bytes of one complete and payload.
      const std::size_t payload_size = sizeof(TargetPayloadType);
  
      // Calculate how many full payloads fit in the incoming message buffer.
      std::size_t full_payloads = size / payload_size;
  
      // Calculate leftover bytes that don't form a complete payload.
      std::size_t leftover_bytes = size % payload_size;

      // RS FIXME - 0cpy variant:
      for (std::size_t i = 0; i < full_payloads; ++i) {
        // Calculate pointer to the i-th payload chunk inside the message buffer.
        // This is a raw reinterpret_cast from char* to TargetPayloadType*,
        // effectively creating a reference directly into the input buffer (zero-copy).
        TargetPayloadType& payload = 
          *reinterpret_cast<TargetPayloadType*>(message + i * payload_size);

        if (m_callback_mode) {
          (*m_sink_callback)(std::move(payload));
        } else {
          if (!m_sink_queue->try_send(std::move(payload), iomanager::Sender::s_no_block)) {
             ++m_dropped_packets;
          }
        }
      } 

/*
      // RS FIXME - MEMCPY variant:
      // Iterate through each full payload in the buffer.
      for (std::size_t i = 0; i < full_payloads; ++i) [[likely]] {{
          // Create a local instance to hold the extracted payload.
          TargetPayloadType payload;
  
          // Copy the raw bytes into our strongly typed payload object.
          // This assumes payload is trivially copyable or POD-like. 
          //   - RS FIXME: TBD to ensure or ommit type safety in the plugin's design 
          // Note: Using std::memcpy avoids undefined behavior from strict aliasing.
          std::memcpy(&payload, message + i * payload_size, payload_size);
  
          if (m_callback_mode) {
              // Callback mode: directly pass the payload to a sink callback.
              // Using std::move allows for efficient transfer if payload supports move semantics.
              (*m_sink_callback)(std::move(payload));
          } else {
              // Queue mode: attempt to enqueue the payload in a non-blocking way.
              if (!m_sink_queue->try_send(std::move(payload), iomanager::Sender::s_no_block)) {
                  // Queue is full or unavailable: record a dropped packet.
                  ++m_dropped_packets;  // total drop counter
              }
          }
      }
*/  

      // If we received bytes that don't form a complete payload...
      if (leftover_bytes > 0) {
          // RS FIXME: Record this as a bad DAQ stream payload for monitoring/statistics purposes.
          //++m_bad_daq_stream_payload_count;
      }
  
      // Function result: true only if:
      // - No leftover bytes remained (i.e., input perfectly aligned to payload size)
      return leftover_bytes == 0;
  }

  void generate_opmon_data() override {
      
    if(m_dropped_packets != 0) {
        ers::warning(FailedToSendData(ERS_HERE, m_sink_id, m_dropped_packets));
    }

    opmon::SourceInfo info;
    info.set_dropped_frames( m_dropped_packets.load() ); 

    publish( std::move(info) );
  }
  
private:
  // Sink internals
  std::string m_sink_id;
  bool m_sink_is_set{ false };
  std::shared_ptr<sink_t> m_sink_queue;

  // Callback internals
  bool m_callback_mode;
  bool m_callback_is_acquired{ false };
  using sink_cb_t = std::shared_ptr<std::function<void(TargetPayloadType&&)>>;
  sink_cb_t m_sink_callback;

  std::atomic<uint64_t> m_dropped_packets{0};

};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_SRC_SOURCEMODEL_HPP_
