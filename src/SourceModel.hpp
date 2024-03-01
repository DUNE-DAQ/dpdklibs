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


#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"
// #include "readoutlibs/utils/ReusableThread.hpp"

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

  std::shared_ptr<sink_t>& get_sink() { return m_sink_queue; }

  bool handle_payload(char* message, std::size_t size) // NOLINT(build/unsigned)
  {
    bool push_out = true;
    if (push_out) {

      TargetPayloadType& target_payload = *reinterpret_cast<TargetPayloadType*>(message);
      if (!m_sink_queue->try_send(std::move(target_payload), iomanager::Sender::s_no_block)) {
        //if(m_dropped_packets == 0 || m_dropped_packets%10000) {
        //  TLOG() << "Dropped data " << m_dropped_packets;
        //}
        ++m_dropped_packets;
      }
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

  // Sink
  bool m_sink_is_set{ false };
  std::shared_ptr<sink_t> m_sink_queue;

  std::atomic<uint64_t> m_dropped_packets{0};

};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_SRC_SOURCEMODEL_HPP_
