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
#include "fdreadoutlibs/pds/DescriptorProcessor.hpp"
#include "fdreadoutlibs/DAPHNEEthTypeAdapter.hpp"
#include <type_traits>

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

  void acquire_callback() override
  {
    if constexpr (std::is_same_v<TargetPayloadType, fdreadoutlibs::types::DAPHNEEthTypeAdapter>) {
      m_descriptor_processor = fdreadoutlibs::pds::get_descriptor_processor(inherited::m_sink_conf->UID());
    }
      if (m_callback_is_acquired) {
        TLOG_DEBUG(5) << "SourceModel callback is already acquired!";
      } else {
        // Getting DataMoveCBRegistry
        auto dmcbr = datahandlinglibs::DataMoveCallbackRegistry::get();
        m_sink_callback = dmcbr->get_callback<TargetPayloadType>(inherited::m_sink_conf);
        m_callback_is_acquired = true;
      }
  }

  bool has_trigger_processor() const override { return bool(m_descriptor_processor); }

  bool trigger_descriptor(char* buffer, std::size_t size, const char*& bytes,
                          std::size_t& length, uint32_t& partition) const override
  {
    if (!m_descriptor_processor || size != m_expected_frame_size) return false;
    fdreadoutlibs::pds::DescriptorFrame descriptor;
    std::memcpy(&descriptor, buffer, sizeof(descriptor));
    bytes = buffer;
    length = sizeof(descriptor);
    partition = descriptor.header.channel;
    return true;
  }

  void handle_trigger_frame(const char* bytes, std::size_t size) override
  {
    if (!m_descriptor_processor || size != sizeof(fdreadoutlibs::pds::DescriptorFrame)) {
      throw std::invalid_argument("Invalid DAPHNE descriptor callback");
    }
    fdreadoutlibs::pds::DescriptorFrame descriptor;
    std::memcpy(&descriptor, bytes, sizeof(descriptor));
    m_descriptor_processor->process(descriptor);
  }

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

        // Pass by value (moved); no references into 'buffer', so no UAF.
        (*m_sink_callback)(std::move(frame));
    }
  }

  void generate_opmon_data() override {
      
    if(m_failed_to_send_daq_payloads != 0) {
      ers::warning(FailedToSendData(ERS_HERE, inherited::m_sink_conf->UID(), m_failed_to_send_daq_payloads));
    }

    opmon::SourceInfo info;
    info.set_failed_to_send_daq_payloads( m_failed_to_send_daq_payloads.exchange(0) );
    info.set_leftover_bytes_encountered( m_leftover_bytes_encountered.exchange(0) );

    if (m_descriptor_processor) {
      auto& c = m_descriptor_processor->counters;
      info.set_descriptor_frames_processed(c.frames.load());
      info.set_descriptor_overflows(c.overflow.load());
      info.set_descriptor_frames_malformed(c.malformed.load());
    }
    publish( std::move(info) );
  }
  
private:
  std::shared_ptr<fdreadoutlibs::pds::DescriptorProcessor> m_descriptor_processor;

  // Constants
  const std::size_t m_expected_frame_size = sizeof(TargetPayloadType);

  // Callback internals
  bool m_callback_is_acquired{ false };
  using sink_cb_t = std::shared_ptr<std::function<void(TargetPayloadType&&)>>;
  sink_cb_t m_sink_callback;

  // Stats
  std::atomic<uint64_t> m_leftover_bytes_encountered{0};
  std::atomic<uint64_t> m_failed_to_send_daq_payloads{0};

};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_SRC_SOURCEMODEL_HPP_
