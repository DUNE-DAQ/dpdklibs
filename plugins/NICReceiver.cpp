/**
 * @file NICReceiver.cpp NICReceiver DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

#include "logging/Logging.hpp"

#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/utils/BufferCopy.hpp" 
#include "fdreadoutlibs/TDEFrameTypeAdapter.hpp"
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
#include <ios>

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

NICReceiver::NICReceiver(const std::string& name)
  : DAQModule(name),
    m_run_marker{ false },
    m_routing_policy{ "incremental" },
    m_prev_sink(0),
    m_next_sink(0)
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
      int sourceid = -1;
      try {
        sourceid = std::stoi(words.back());
      } catch (const std::exception& ex) {
        ers::fatal(dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "Output link ID could not be parsed on queue instance name! "));
      }
      TLOG() << "Creating source for target queue: " << target << " DLH number: " << sourceid;
      m_sources[sourceid] = createSourceModel(qi.uid);
      if (m_sources[sourceid] == nullptr) {
        ers::fatal(dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "CreateSource failed to provide an appropriate model for queue!"));
      }
      m_sources[sourceid]->init(args);
    }
  }
}

void
NICReceiver::do_configure(const data_t& args)
{
  TLOG() << get_name() << ": Entering do_conf() method";
  m_cfg = args.get<module_conf_t>();
  m_iface_id = (uint16_t)m_cfg.card_id;
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
    m_num_bytes[src.id] = { 0 };
  }

  // Setup SourceConcepts
  // m_sources[]->configure if needed!?

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

  // Setting up interface
  TLOG() << "Initialize interface " << m_iface_id;
  ealutils::iface_init(m_iface_id, m_rx_qs.size(), 0, m_mbuf_pools); // 0 = no TX queues
  // Promiscuous mode
  ealutils::iface_promiscuous_mode(m_iface_id, false); // should come from config

  // Flow steering setup
  TLOG() << "Configuring Flow steering rules.";
  struct rte_flow_error error;
  struct rte_flow *flow;
  TLOG() << "Attempt to flush previous flow rules...";
  rte_flow_flush(m_iface_id, &error);
#warning RS: FIXME -> Check for flow flush return!
  for (auto const& [lcoreid, rxqs] : m_rx_core_map) {
    for (auto const& [rxqid, srcip] : rxqs) {

      // Put the IP numbers temporarily in a vector, so they can be converted easily to uint32_t
      TLOG() << "Current ip is " << srcip;
      size_t ind = 0, current_ind = 0;
      std::vector<uint8_t> v;
      for (int i = 0; i < 4; ++i) {
        v.push_back(std::stoi(srcip.substr(current_ind, srcip.size() - current_ind), &ind));
        current_ind += ind + 1;
      }

      flow = generate_ipv4_flow(m_iface_id, rxqid, 
		    RTE_IPV4(v[0], v[1], v[2], v[3]), 0xffffffff, 0, 0, &error);

      if (not flow) { // ers::fatal
        TLOG() << "Flow can't be created for " << rxqid
	       << " Error type: " << (unsigned)error.type
	       << " Message: " << error.message;
        ers::fatal(dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "Couldn't create Flow API rules!"));
    	  rte_exit(EXIT_FAILURE, "error in creating flow");
      }
    }
  }

  if (m_cfg.with_drop_flow) {
    // Adding drop flow
    TLOG() << "Adding Drop Flow.";
    flow = generate_drop_flow(m_iface_id, &error);
    if (not flow) { // ers::fatal
      TLOG() << "Drop flow can't be created for interface!"
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
	      for (auto& [qid, nframes] : m_num_frames) { // check for new frames
          if (nframes.load() > 0) {
            auto nbytes = m_num_bytes[qid].load();
  	        TLOG() << "Received payloads of q[" << qid << "] is: " << nframes.load()
                   << " Bytes: " << nbytes << " Rate: " << nbytes / 1e6 * 8 << " Mbps";
	          nframes.exchange(0);
            m_num_bytes[qid].exchange(0);
          }
        }
        for (auto& [strid, nframes] : m_num_unexid_frames) { // check for unexpected StreamID frames
          if (nframes.load() > 0) {
            TLOG() << "Unexpected StreamID frames with strid[" << strid << "]! Num: " << nframes.load();
            nframes.exchange(0);
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });

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
    if (m_stat_thread.joinable()) {
      m_stat_thread.join();
    } else {
      TLOG() << "Stats thread is not joinable!";
    }
    TLOG() << "Stoppped DPDK lcore processors and internal threads...";
  } else {
    TLOG_DEBUG(5) << "DPDK lcore processor is already stopped!";
  }
}

void
NICReceiver::do_scrap(const data_t&)
{
  TLOG() << get_name() << ": Entering do_scrap() method";
  struct rte_flow_error error;
  rte_flow_flush(m_iface_id, &error);
}

void
NICReceiver::get_info(opmonlib::InfoCollector& ci, int level)
{
  nicreaderinfo::Info nri;
  nri.groups_sent = m_groups_sent.exchange(0);
  nri.total_groups_sent = m_total_groups_sent.load();
}

void
NICReceiver::handle_eth_payload(int src_rx_q, char* payload, std::size_t size) {
  // Get DAQ Header and its StreamID
  auto* daq_header = reinterpret_cast<dunedaq::detdataformats::DAQEthHeader*>(payload);
  auto strid = (unsigned)daq_header->stream_id;
  if (m_sources.count(strid) != 0) {
    auto ret = m_sources[strid]->handle_payload(payload, size);
  } else {
    // Really bad -> unexpeced StreamID in UDP Payload.
    // This check is needed in order to avoid dynamically add thousands
    // of Sources on the fly, in case the data corruption is extremely severe.
    if (m_num_unexid_frames.count(strid) == 0) {
      m_num_unexid_frames[strid] = 0;
    }
    m_num_unexid_frames[strid]++;
  }
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
