/**
 * @file NICReceiver.cpp NICReceiver DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

#include "logging/Logging.hpp"
#include "detdataformats/tde/TDE16Frame.hpp"

#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/utils/BufferCopy.hpp" 
#include "fdreadoutlibs/TDEAMCFrameTypeAdapter.hpp"
#include "fdreadoutlibs/DUNEWIBEthTypeAdapter.hpp"

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/FlowControl.hpp"
#include "CreateSource.hpp"
#include "NICReceiver.hpp"

#include <cinttypes>
#include <chrono>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * @brief Name used by TRACE TLOG calls from this source file
 */
#define TRACE_NAME "NICReceiver" // NOLINT

/**
 * @brief TRACE debug levels used in this source file
 */
enum
{
  TLVL_ENTER_EXIT_METHODS = 5,
  TLVL_WORK_STEPS = 10,
  TLVL_BOOKKEEPING = 15
};

namespace dunedaq {
namespace dpdklibs {

class TDEFrameGrouper
{
public:
  void group(std::vector<std::vector<detdataformats::tde::TDE16Frame>>& v,
             detdataformats::tde::TDE16Frame* frames);

private:
};

void
TDEFrameGrouper::group(std::vector<std::vector<detdataformats::tde::TDE16Frame>>& v,
                       detdataformats::tde::TDE16Frame* frames)
{
  for (int i = 0; i < 12 * 64; i++) {
    v[frames[i].get_tde_header()->slot][frames[i].get_tde_header()->link] = frames[i];
  }
}

NICReceiver::NICReceiver(const std::string& name)
  : DAQModule(name),
    m_run_marker{ false }
{
  register_command("conf", &NICReceiver::do_configure);
  register_command("start", &NICReceiver::do_start);
  register_command("stop", &NICReceiver::do_stop);
  register_command("scrap", &NICReceiver::do_scrap);
}

NICReceiver::~NICReceiver()
{
  TLOG() << get_name() << ": Destructor called. Tearing down EAL.";
  ealutils::finish_eal();
}

inline void
tokenize(std::string const& str, const char delim, std::vector<std::string>& out)
{
  std::size_t start;
  std::size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    out.push_back(str.substr(start, end - start));
  }
}

void
NICReceiver::init(const data_t& args)
{
 auto ini = args.get<appfwk::app::ModInit>();
 for (const auto& qi : ini.conn_refs) {
    if (qi.uid == "errored_chunks_q") {
      continue;
    } else {
      TLOG_DEBUG(TLVL_WORK_STEPS) << ": NICCardReader output queue is " << qi.uid;
      const char delim = '_';
      std::string target = qi.uid;
      std::vector<std::string> words;
      tokenize(target, delim, words);
      int linkid = -1;
      try {
        linkid = std::stoi(words.back());
      } catch (const std::exception& ex) {
        ers::fatal(dunedaq::readoutlibs::InitializationError(ERS_HERE, "Link ID could not be parsed on queue instance name! "));
      }
      TLOG() << "Creating link for target queue: " << target << " DLH number: " << linkid;
			//m_elinks[linkid] = createElinkModel(qi.uid);
			// RS FIXME: Introduce LinkConcepts for different FE types, as this will be a nightmare on the long run...
      m_wib_sender[linkid] = get_iom_sender<fdreadoutlibs::types::DUNEWIBEthTypeAdapter>(qi.uid);

      //if (m_wib_sender[qi.uid] == nullptr) {
      //  ers::fatal(InitializationError(ERS_HERE, "CreateElink failed to provide an appropriate model for queue!"));
      //}
      //m_elinks[linkid]->init(args, m_block_queue_capacity);
    }
  }

  //auto datatypes = dunedaq::iomanager::IOManager::get()->get_datatypes(conn_uid);
  //if (datatypes.size() != 1) {
  //  ers::error(dunedaq::readoutlibs::GenericConfigurationError(ERS_HERE,
  //    "Multiple output data types specified! Expected only a single type!"));
  //}
  //std::string raw_dt{ *datatypes.begin() };
  //TLOG() << "Choosing specializations for ElinkModel for output connection "
  //       << " [uid:" << conn_uid << " , data_type:" << raw_dt << ']';


  // RS FIXME: Remove this attrocity
  m_sender = get_iom_sender<fdreadoutlibs::types::TDEAMCFrameTypeAdapter>("tde_link_0");
}

void
NICReceiver::do_configure(const data_t& args)
{
  TLOG() << get_name() << ": Entering do_conf() method";
  m_cfg = args.get<module_conf_t>();
  m_dest_ip = m_cfg.dest_ip;
  auto ip_sources = m_cfg.ip_sources;
  auto rx_cores = m_cfg.rx_cores; 
  m_num_ip_sources = ip_sources.size();
  m_num_rx_cores = rx_cores.size();

  // Initialize RX core map
  for (auto rxc : rx_cores) {
    for (auto qid : rxc.rx_qs) {
      m_rx_core_map[rxc.lcore_id][qid] = "";
    }
  }

  // Setup expected IP sources
  for (auto src : ip_sources) {
    TLOG() << "IP source to register: ID=" << src.id << " IP=" << src.ip << " RX_Q=" << src.rx_q << " LC=" << src.lcore;
    // Extend mapping
    m_rx_core_map[src.lcore][src.rx_q] = src.ip;    
    m_rx_qs.insert(src.rx_q);

    // Create frame counter metric
    m_num_frames[src.id] = { 0 };
    // Creating AMC handler
    TLOG() << "Setting up AMC[" << src.id << "] data queue and parser...";
    m_amc_data_queues[src.id] = std::make_unique<amc_frame_queue_t>(m_amc_queue_capacity);
    m_amc_frame_handlers[src.id] = std::make_unique<readoutlibs::ReusableThread>(0);
    m_amc_frame_handlers[src.id]->set_name(m_parser_thread_name, src.id);
  }

  // EAL setup
  TLOG() << "Setting up EAL with params from config.";
  std::vector<char*> eal_params = ealutils::string_to_eal_args(m_cfg.eal_arg_list);
  ealutils::init_eal(eal_params.size(), eal_params.data());

  // Allocate pools and mbufs per queue
  TLOG() << "Allocating pools and mbufs.";
  for (size_t i=0; i<m_rx_qs.size(); ++i) {
    std::stringstream ss;
    ss << "MBP-" << i;
    TLOG() << "Pool acquire: " << ss.str(); 
    m_mbuf_pools[i] = ealutils::get_mempool(ss.str());
    m_bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
    rte_pktmbuf_alloc_bulk(m_mbuf_pools[i].get(), m_bufs[i], m_burst_size);
  }

  // Setting up only port0
  TLOG() << "Initialize only port 0!";
  ealutils::port_init(0, m_rx_qs.size(), 0, m_mbuf_pools); // just init port0, no TX queues

  // Flow steering setup
// RS FIXME: DISABLE FOR NOW!
  TLOG() << "Configuring Flow steering rules.";
  struct rte_flow_error error;
  struct rte_flow *flow;
if (false) {
  for (auto const& [lcoreid, rxqs] : m_rx_core_map) {
    for (auto const& [rxqid, srcip] : rxqs) {

      // Put the IP numbers temporarily in a vector, so they can be converted easily to uint32_t
      TLOG() << "Current ip is " << srcip;
      size_t ind = 0, current_ind = 0;
      std::vector<uint8_t> v;
      for (int i = 0; i < 4; ++i) {
        TLOG() << "Calling stoi with argument " << srcip.substr(current_ind, srcip.size() - current_ind);
        v.push_back(std::stoi(srcip.substr(current_ind, srcip.size() - current_ind), &ind));
        current_ind += ind + 1;
      }

      flow = generate_ipv4_flow(0, rxqid, 
		                RTE_IPV4(v[0], v[1], v[2], v[3]), 0xffffffff,
				0, 0,
				&error);
      if (not flow) { // ers::fatal
        TLOG() << "Flow can't be created for " << rxqid
	       << " Error type: " << (unsigned)error.type
	       << " Message: " << error.message;
    	rte_exit(EXIT_FAILURE, "error in creating flow");
      }
    }
  }
}

  if (m_cfg.with_drop_flow) {
    // Adding drop flow
    TLOG() << "Adding Drop Flow.";
    flow = generate_drop_flow(0, &error);
    if (not flow) { // ers::fatal
      TLOG() << "Drop flow can't be created for port0!"
             << " Error type: " << (unsigned)error.type
             << " Message: " << error.message;
      rte_exit(EXIT_FAILURE, "error in creating flow");
    }
  }
  
  TLOG() << "DPDK EAL & RTE configured.";
}

void
NICReceiver::do_start(const data_t&)
{
  TLOG() << get_name() << ": Entering do_start() method";
  if (!m_run_marker.load()) {
    set_running(true);
    m_dpdk_quit_signal = 0;
    ealutils::dpdk_quit_signal = 0;

    TLOG() << "Starting stats thread.";
    m_stat_thread = std::thread([&]() {
      while (m_run_marker.load()) {
        // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
	for (auto& [qid, nframes] : m_num_frames) { // fixme for proper payload size
	  TLOG() << "Received Rate of q[" << qid << "] is " << size_t(9000) * nframes.load() / 1e6 * 8;
	  nframes.exchange(0);
	}
	TLOG() << "CLEARED  Rate is " << size_t(9000) * m_cleaned / 1e6 * 8;
	m_cleaned.exchange(0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });

    TLOG() << "Starting frame processors.";
    // for (unsigned int i=0; i<m_amc_frame_handlers.size(); ++i) {
    //   m_amc_frame_handlers[i]->set_work(&NICReceiver::handle_frame_queue, this, i);
    // }

    TLOG() << "Starting LCore processors:";
    for (auto const& [lcoreid, rxqs] : m_rx_core_map) {
      int ret = rte_eal_remote_launch((int (*)(void*))(&NICReceiver::rx_runner), this, lcoreid);
      TLOG() << "  -> LCore[" << lcoreid << "] launched with return code=" << ret;
    }

  } else {
    TLOG_DEBUG(5) << "NICReader is already running!";
  }
}

void
NICReceiver::do_stop(const data_t&)
{
  TLOG() << get_name() << ": Entering do_stop() method";
  if (m_run_marker.load()) {
    TLOG() << "Raising stop through variables!";
    set_running(false);
    m_dpdk_quit_signal = 1;
    ealutils::dpdk_quit_signal = 1;
    ealutils::wait_for_lcores();
    TLOG() << "Stoppped DPDK lcore processors...";
    struct rte_flow_error error;
    rte_flow_flush(0, &error);
  } else {
    TLOG_DEBUG(5) << "DPDK lcore processor is already stopped!";
  }
}

void
NICReceiver::do_scrap(const data_t&)
{
  TLOG() << get_name() << ": Entering do_scrap() method";
}

void
NICReceiver::get_info(opmonlib::InfoCollector& ci, int level)
{
  nicreaderinfo::Info nri;
  nri.groups_sent = m_groups_sent.exchange(0);
  nri.total_groups_sent = m_total_groups_sent.load();
}

void 
NICReceiver::handle_frame_queue(int id)
{
  TLOG() << "frame_proc[" << id << "] starting to handle frames in corresponding queue!";
  while (m_run_marker.load()) {
    detdataformats::tde::TDE16Frame frame;
    if (m_amc_data_queues[id]->read(frame)) {
      m_cleaned++;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

void
NICReceiver::copy_out(int queue, char* message, std::size_t size) {
  //detdataformats::tde::TDE16Frame target_payload;

  fdreadoutlibs::types::DUNEWIBEthTypeAdapter target_payload;
  uint32_t bytes_copied = 0;
  readoutlibs::buffer_copy(message, size, static_cast<void*>(&target_payload), bytes_copied, sizeof(target_payload));

  // first frame's streamID:
  auto streamid = (unsigned)target_payload.begin()->daq_header.stream_id;
  m_wib_sender[streamid]->send(std::move(target_payload), std::chrono::milliseconds(100));

}

void 
NICReceiver::set_running(bool should_run)
{
  bool was_running = m_run_marker.exchange(should_run);
  TLOG_DEBUG(5) << "Active state was toggled from " << was_running << " to " << should_run;
}

} // namespace dpdklibs
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICReceiver)
