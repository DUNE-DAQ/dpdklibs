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
//#include "dpdklibs/felixcardreaderinfo/InfoNljs.hpp"
#include "logging/Logging.hpp"
#include "readoutlibs/utils/ReusableThread.hpp"

#include <folly/ProducerConsumerQueue.h>
#include <nlohmann/json.hpp>

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

  std::shared_ptr<sink_t>& get_sink() { return m_sink_queue; }

  //std::shared_ptr<err_sink_t>& get_error_sink() { return m_error_sink_queue; }

  void init(const data_t& /*args*/)
  {
    //m_block_addr_queue = std::make_unique<folly::ProducerConsumerQueue<uint64_t>>(block_queue_capacity); // NOLINT
  }

  void conf(const data_t& /*args*/)
  {
    if (m_configured) {
      TLOG_DEBUG(5) << "SourceModel is already configured!";
    } else {
      //m_parser_thread.set_name(inherited::m_elink_source_tid, inherited::m_link_tag);
      // if (inconsistency)
      // ers::fatal(ElinkConfigurationInconsistency(ERS_HERE, m_num_links));

      //m_parser->configure(block_size, is_32b_trailers); // unsigned bsize, bool trailer_is_32bit
      m_configured = true;
    }
  }

  void start(const data_t& /*args*/)
  {
    m_t0 = std::chrono::high_resolution_clock::now();
    if (!m_run_marker.load()) {
      set_running(true);
      //m_parser_thread.set_work(&SourceModel::process_elink, this);
      TLOG_DEBUG(5) << "Started SourceModel of link " << inherited::m_opmon_str << "...";
    } else {
      TLOG_DEBUG(5) << "SourceModel of link " << inherited::m_opmon_str << " is already running!";
    }
  }

  void stop(const data_t& /*args*/)
  {
    if (m_run_marker.load()) {
      set_running(false);
      //while (!m_parser_thread.get_readiness()) {
      //  std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

 
  /* 
  void
  NICReceiver::copy_out(int queue, char* message, std::size_t size) {
    //fddetdataformats::TDE16Frame target_payload;
  
    fdreadoutlibs::types::DUNEWIBEthTypeAdapter target_payload;
    uint32_t bytes_copied = 0;
    dump_to_buffer(message, size, static_cast<void*>(&target_payload), bytes_copied, sizeof(target_payload));
  
    // first frame's streamID:
    auto streamid = (unsigned)target_payload.begin()->daq_header.stream_id;
    m_wib_sender[streamid]->send(std::move(target_payload), std::chrono::milliseconds(100));
  
  }*/


  bool handle_payload(char* message, std::size_t size) // NOLINT(build/unsigned)
  {
    // TargetPayloadType target_payload;
    //TLOG() << "Type of target_payload: " << typeid(target_payload).name();
    //TLOG() << "Size of target_payload: " << (unsigned)sizeof(target_payload);
    //TLOG() << "Bytes to be copied: " << size;
    // uint32_t bytes_copied = 0;
    // readoutlibs::buffer_copy(message, size, static_cast<void*>(&target_payload), bytes_copied, sizeof(target_payload));
    //TLOG() << "PAYLOAD READY WITH SIZE: " << bytes_copied;
    
    //m_sink_queue->send(std::move(target_payload), std::chrono::milliseconds(100));

    TargetPayloadType& target_payload = *reinterpret_cast<TargetPayloadType*>(message);
    if (!m_sink_queue->try_send(std::move(target_payload), iomanager::Sender::s_no_block)) {
      if(m_dropped_packets == 0 || m_dropped_packets%10000) {
        TLOG() << "Dropped data " << m_dropped_packets;
      }
      ++m_dropped_packets;
    }

    //TLOG() << "SENT!";
    return true;
    //if (m_block_addr_queue->write(block_addr)) { // ok write
    //  return true;
    //} else { // failed write
    //  return false;
    //}
  }

  void get_info(opmonlib::InfoCollector& ci, int /*level*/)
  {
    //felixcardreaderinfo::ELinkInfo info;
    //auto now = std::chrono::high_resolution_clock::now();
    //auto& stats = m_parser_impl.get_stats();

    //info.card_id = m_card_id;
    //info.logical_unit = m_logical_unit;
    //info.link_id = m_link_id;
    //info.link_tag = m_link_tag;

    //double seconds = std::chrono::duration_cast<std::chrono::microseconds>(now - m_t0).count() / 1000000.;

    //info.num_short_chunks_processed = stats.short_ctr.exchange(0);
    //info.num_chunks_processed = stats.chunk_ctr.exchange(0);
    //info.num_subchunks_processed = stats.subchunk_ctr.exchange(0);
    //info.num_blocks_processed = stats.block_ctr.exchange(0);
    //info.num_short_chunks_processed_with_error = stats.error_short_ctr.exchange(0);
    //info.num_chunks_processed_with_error = stats.error_chunk_ctr.exchange(0);
    //info.num_subchunks_processed_with_error = stats.error_subchunk_ctr.exchange(0);
    //info.num_blocks_processed_with_error = stats.error_block_ctr.exchange(0);
    //info.num_subchunk_crc_errors = stats.subchunk_crc_error_ctr.exchange(0);
    //info.num_subchunk_trunc_errors = stats.subchunk_trunc_error_ctr.exchange(0);
    //info.num_subchunk_errors = stats.subchunk_error_ctr.exchange(0);
    //info.rate_blocks_processed = info.num_blocks_processed / seconds / 1000.;
    //info.rate_chunks_processed = info.num_chunks_processed / seconds / 1000.;

    //TLOG_DEBUG(2) << inherited::m_elink_str // Move to TLVL_TAKE_NOTE from readout
    //              << " Parser stats ->"
    //              << " Blocks: " << info.num_blocks_processed << " Block rate: " << info.rate_blocks_processed
    //              << " [kHz]"
    //              << " Chunks: " << info.num_chunks_processed << " Chunk rate: " << info.rate_chunks_processed
    //              << " [kHz]"
    //              << " Shorts: " << info.num_short_chunks_processed << " Subchunks:" << info.num_subchunks_processed
    //              << " Error Chunks: " << info.num_chunks_processed_with_error
    //              << " Error Shorts: " << info.num_short_chunks_processed_with_error
    //              << " Error Subchunks: " << info.num_subchunks_processed_with_error
    //              << " Error Block: " << info.num_blocks_processed_with_error;

    //m_t0 = now;

    //opmonlib::InfoCollector child_ci;
    //child_ci.add(info);

    //ci.add(m_opmon_str, child_ci);
  }

private:
  // Types
  //using UniqueBlockAddrQueue = std::unique_ptr<folly::ProducerConsumerQueue<uint64_t>>; // NOLINT(build/unsigned)

  // Internals
  std::atomic<bool> m_run_marker;
  bool m_configured{ false };

  // Sink
  bool m_sink_is_set{ false };
  std::shared_ptr<sink_t> m_sink_queue;
  //std::shared_ptr<err_sink_t> m_error_sink_queue;

  std::atomic<uint64_t> m_dropped_packets{0};

  // blocks to process
  //UniqueBlockAddrQueue m_block_addr_queue;

  // Processor
  //inline static const std::string m_parser_thread_name = "elinkp";
  //readoutlibs::ReusableThread m_parser_thread;
  //void process_elink()
  //{
  //  while (m_run_marker.load()) {
  //    uint64_t block_addr;                        // NOLINT
  //    if (m_block_addr_queue->read(block_addr)) { // read success
  //      const auto* block = const_cast<felix::packetformat::block*>(
  //        felix::packetformat::block_from_bytes(reinterpret_cast<const char*>(block_addr)) // NOLINT
  //      );
  //      m_parser->process(block);
  //    } else { // couldn't read from queue
  //      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  //    }
  //  }
  //}
};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_SRC_SOURCEMODEL_HPP_
