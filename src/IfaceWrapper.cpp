/**
 * @file IfaceWrapper.cpp DPDK based Interface wrapper
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "logging/Logging.hpp"
#include "readoutlibs/ReadoutIssues.hpp"

#include "dpdklibs/Issues.hpp"

#include "dpdklibs/nicreader/Structs.hpp"
#include "dpdklibs/nicreaderinfo/InfoNljs.hpp"

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/FlowControl.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/arp/ARP.hpp"
#include "dpdklibs/ipv4_addr.hpp"
#include "IfaceWrapper.hpp"

#include "appfwk/ConfigurationManager.hpp"
// #include "confmodel/DROStreamConf.hpp"
// #include "confmodel/StreamParameters.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/ProcessingResource.hpp"
#include "appmodel/DPDKPortConfiguration.hpp"
// #include "confmodel/NetworkDevice.hpp"
// #include "appmodel/NICInterfaceConfiguration.hpp"
// #include "appmodel/NICStatsConf.hpp"
// #include "appmodel/EthStreamParameters.hpp"

#include <chrono>
#include <memory>
#include <string>

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


//-----------------------------------------------------------------------------
IfaceWrapper::IfaceWrapper(
  uint iface_id,
  const appmodel::DPDKReceiver* receiver,
  const std::vector<const appmodel::NWDetDataSender*>& nw_senders,
  sid_to_source_map_t& sources,
  std::atomic<bool>& run_marker
  )
    : m_sources(sources)
    , m_run_marker(run_marker)
{ 
  auto net_device = receiver->get_uses()->cast<confmodel::NetworkDevice>();

  m_iface_id = iface_id;
  m_mac_addr = net_device->get_mac_address();
  m_ip_addr = net_device->get_ip_address();
  IpAddr ip_addr_struct(m_ip_addr);
  m_ip_addr_bin = udp::ip_address_dotdecimal_to_binary(
      ip_addr_struct.addr_bytes[3],
      ip_addr_struct.addr_bytes[2],
      ip_addr_struct.addr_bytes[1],
      ip_addr_struct.addr_bytes[0]
  );

  auto iface_cfg = receiver->get_configuration();

  m_with_flow = iface_cfg->get_flow_control();
  m_prom_mode = iface_cfg->get_promiscuous_mode();;
  m_mtu = iface_cfg->get_mtu();
  m_rx_ring_size = iface_cfg->get_rx_ring_size();
  m_tx_ring_size = iface_cfg->get_tx_ring_size();
  m_num_mbufs = iface_cfg->get_num_bufs();
  m_burst_size = iface_cfg->get_burst_size();
  m_mbuf_cache_size = iface_cfg->get_mbuf_cache_size();

  m_lcore_sleep_ns = iface_cfg->get_lcore_sleep_us() * 1000;
  m_socket_id = rte_eth_dev_socket_id(m_iface_id);

  m_iface_id_str = iface_cfg->UID();


  // Here is my list of cores
  for( const auto* proc_res : iface_cfg->get_used_lcores()) {
    m_rte_cores.insert(m_rte_cores.end(), proc_res->get_cpu_cores().begin(), proc_res->get_cpu_cores().end());
  }

  // iterate through active streams

  // Create a map of sender ni (ip) to streams
  std::map<std::string, std::map<uint, uint>> ip_to_stream_src_groups;

  for( auto nw_sender : nw_senders ) {
    auto sender_ni = nw_sender->get_uses();

    std::string tx_ip = sender_ni->get_ip_address();

    for ( auto res : nw_sender->get_contains() ) {

      auto det_stream = res->cast<confmodel::DetectorStream>();
      uint32_t tx_geo_stream_id = det_stream->get_geo_id()->get_stream_id();
      ip_to_stream_src_groups[tx_ip][tx_geo_stream_id] = det_stream->get_source_id();
      TLOG() << fmt::format("AAAAAAAA {} -> [{} -> {}]", tx_ip, tx_geo_stream_id, det_stream->get_source_id());
    }

  }

  uint32_t core_idx(0), rx_q(0);
  for( const auto& [tx_ip, strm_src] : ip_to_stream_src_groups) {
    m_ips.insert(tx_ip);
    m_rx_qs.insert(rx_q);
    m_num_frames_rxq[rx_q] = { 0 };
    m_num_bytes_rxq[rx_q] = { 0 };

    m_rx_core_map[m_rte_cores[core_idx]][rx_q] = tx_ip;
    m_stream_id_to_source_id[rx_q] = strm_src;

    ++rx_q;
    if ( ++core_idx == m_rte_cores.size()) {
      core_idx = 0;
    }
  }

  // Log mapping
  TLOG() << "--- Core to queue->ip mapping ---";
  for (auto const& [lcore, rx_qs] : m_rx_core_map) {
    TLOG() << fmt::format("Lcore={} handles:", lcore);
    for (auto const& [rx_q, src_ip] : rx_qs) {
      TLOG() << fmt::format(" rx_q={} src_ip={}", rx_q, src_ip);
    }
  }

  TLOG() << "--- Queue to [stream_id -> sid] mapping";
  for (auto const& [rx_q, strms] : m_stream_id_to_source_id) {
    TLOG() << fmt::format("Queue : {}",rx_q);
    for (auto const& [strm, srcid] : strms) {
      TLOG() << fmt::format("  strm={} -> srcid={}", strm, srcid);
    }
  }

  // Adding single TX queue for ARP responses
  TLOG() << "Append TX_Q=0 for ARP responses.";
  m_tx_qs.insert(0);

}


//-----------------------------------------------------------------------------
IfaceWrapper::~IfaceWrapper()
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << "IfaceWrapper destructor called. First stop check, then closing iface.";
    
  struct rte_flow_error error;
  rte_flow_flush(m_iface_id, &error);
  //graceful_stop();
  //close_iface();
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << "IfaceWrapper destroyed.";
}


//-----------------------------------------------------------------------------
void
IfaceWrapper::allocate_mbufs() 
{
  TLOG() << "Allocating pools and mbufs.";
  for (size_t i=0; i<m_rx_qs.size(); ++i) {
    std::stringstream ss;
    ss << "MBP-" << m_iface_id << '-' << i;
    TLOG() << "Acquire pool with name=" << ss.str() << " for iface_id=" << m_iface_id << " rxq=" << i;
    m_mbuf_pools[i] = ealutils::get_mempool(ss.str(), m_num_mbufs, m_mbuf_cache_size, 16384, m_socket_id);
    m_bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
    // No need to alloc?
    // rte_pktmbuf_alloc_bulk(m_mbuf_pools[i].get(), m_bufs[i], m_burst_size);
  }

  std::stringstream ss;
  ss << "GARPMBP-" << m_iface_id;
  TLOG() << "Acquire GARP pool with name=" << ss.str() << " for iface_id=" << m_iface_id;
  m_garp_mbuf_pool = ealutils::get_mempool(ss.str());
  m_garp_bufs[0] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
  rte_pktmbuf_alloc_bulk(m_garp_mbuf_pool.get(), m_garp_bufs[0], m_burst_size);
}


//-----------------------------------------------------------------------------
void
IfaceWrapper::setup_interface()
{
  TLOG() << "Initialize interface " << m_iface_id;
  bool with_reset = true, with_mq_mode = true; // go to config
  bool check_link_status = false;

  int retval = ealutils::iface_init(m_iface_id, m_rx_qs.size(), m_tx_qs.size(), m_rx_ring_size, m_tx_ring_size, m_mbuf_pools, with_reset, with_mq_mode, check_link_status);
  if (retval != 0 ) {
    throw FailedToSetupInterface(ERS_HERE, m_iface_id, retval);
  }
  // Promiscuous mode
  ealutils::iface_promiscuous_mode(m_iface_id, m_prom_mode); // should come from config
}


//-----------------------------------------------------------------------------
void
IfaceWrapper::setup_flow_steering()
{
  // Flow steering setup
  TLOG() << "Configuring Flow steering rules for iface=" << m_iface_id;
  struct rte_flow_error error;
  struct rte_flow *flow;
  TLOG() << "Attempt to flush previous flow rules...";
  rte_flow_flush(m_iface_id, &error);
#warning RS: FIXME -> Check for flow flush return!
  for (auto const& [lcoreid, rxqs] : m_rx_core_map) {
    for (auto const& [rxqid, srcip] : rxqs) {
      // Put the IP numbers temporarily in a vector, so they can be converted easily to uint32_t
      TLOG() << "Creating flow rule for src_ip=" << srcip << " assigned to rxq=" << rxqid;
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

  return;
}

//-----------------------------------------------------------------------------
void
IfaceWrapper::setup_xstats() 
{
  // Stats setup
  m_iface_xstats.setup(m_iface_id);
  m_iface_xstats.reset_counters();
}


//-----------------------------------------------------------------------------
void
IfaceWrapper::start()
{
  for (auto const& [rx_q, _] : m_num_frames_rxq ) {
    m_num_frames_rxq[rx_q] = { 0 };
    m_num_bytes_rxq[rx_q] = { 0 };
    m_num_full_bursts[rx_q] = { 0 };
    m_max_burst_size[rx_q] = { 0 };
  }
  
  
  m_lcore_enable_flow.store(false);
  m_lcore_quit_signal.store(false);
  TLOG() << "Launching GARP thread with garp_func...";
  m_garp_thread = std::thread(&IfaceWrapper::garp_func, this);
  

  TLOG() << "Interface id=" << m_iface_id << " starting LCore processors:";
  for (auto const& [lcoreid, _] : m_rx_core_map) {
    int ret = rte_eal_remote_launch((int (*)(void*))(&IfaceWrapper::rx_runner), this, lcoreid);
    TLOG() << "  -> LCore[" << lcoreid << "] launched with return code=" << ret;
  }
}

//-----------------------------------------------------------------------------
void
IfaceWrapper::stop()
{
  m_lcore_enable_flow.store(false);
  m_lcore_quit_signal.store(true);
  // Stop GARP sender thread  
  if (m_garp_thread.joinable()) {
    m_garp_thread.join();
  } else {
    TLOG() << "GARP thrad is not joinable!";
  }
}
/*
void
IfaceWrapper::scrap()
{
  struct rte_flow_error error;
  rte_flow_flush(m_iface_id, &error);
}
*/


//-----------------------------------------------------------------------------
void 
IfaceWrapper::get_info(opmonlib::InfoCollector& ci, int level)
{

  nicreaderinfo::EthStats s;
  s.ipackets = m_iface_xstats.m_eth_stats.ipackets;
  s.opackets = m_iface_xstats.m_eth_stats.opackets;
  s.ibytes = m_iface_xstats.m_eth_stats.ibytes;
  s.obytes = m_iface_xstats.m_eth_stats.obytes;
  s.imissed = m_iface_xstats.m_eth_stats.imissed;
  s.ierrors = m_iface_xstats.m_eth_stats.ierrors;
  s.oerrors = m_iface_xstats.m_eth_stats.oerrors;
  s.rx_nombuf = m_iface_xstats.m_eth_stats.rx_nombuf;
  ci.add(s);

  // Empty stat JSON placeholder
  nlohmann::json stat_json;

  // Poll stats from HW
  m_iface_xstats.poll();

  // Build JSON from values 
  for (int i = 0; i < m_iface_xstats.m_len; ++i) {
    stat_json[m_iface_xstats.m_xstats_names[i].name] = m_iface_xstats.m_xstats_values[i];
  }

  // Reset HW counters
  m_iface_xstats.reset_counters();

  // Convert JSON to NICReaderInfo struct
  nicreaderinfo::EthXStats xs;
  nicreaderinfo::from_json(stat_json, xs);

  // Push to InfoCollector
  ci.add(xs);
  TLOG_DEBUG(TLVL_WORK_STEPS) << "opmonlib::InfoCollector object passed by reference to IfaceWrapper::get_info"
    << " -> Result looks like the following:\n" << ci.get_collected_infos();

  for( const auto& [src_rx_q,_] : m_num_frames_rxq) {
    nicreaderinfo::QueueStats qs;
    qs.packets_received = m_num_frames_rxq[src_rx_q].load();
    qs.bytes_received = m_num_bytes_rxq[src_rx_q].load();
    qs.full_rx_burst = m_num_full_bursts[src_rx_q].load();
    qs.max_burst_size = m_max_burst_size[src_rx_q].exchange(0);

    opmonlib::InfoCollector queue_ci;
    queue_ci.add(qs);

    ci.add(fmt::format("queue_{}", src_rx_q), queue_ci);
  }

  for ( const auto& [src_id, src_obj] : m_sources ) {    
    opmonlib::InfoCollector src_ci;
    src_obj->get_info(src_ci, level);
    ci.add(fmt::format("src_{}", src_id), src_ci);
  }

}

//-----------------------------------------------------------------------------
void
IfaceWrapper::garp_func()
{  
  TLOG() << "Launching GARP sender...";
  while(m_run_marker.load()) {
    arp::pktgen_send_garp(m_garp_bufs[0][0], m_iface_id, m_ip_addr_bin);   
    ++m_garps_sent;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  TLOG() << "GARP function joins.";
}

//-----------------------------------------------------------------------------
void
IfaceWrapper::handle_eth_payload(int src_rx_q, char* payload, std::size_t size)
{  
  // Get DAQ Header and its StreamID
  auto* daq_header = reinterpret_cast<dunedaq::detdataformats::DAQEthHeader*>(payload);
  auto src_id = m_stream_id_to_source_id[src_rx_q][(unsigned)daq_header->stream_id];

  if ( auto src_it = m_sources.find(src_id); src_it != m_sources.end()) {
    src_it->second->handle_payload(payload, size);
  } else {
    // Really bad -> unexpeced StreamID in UDP Payload.
    // This check is needed in order to avoid dynamically add thousands
    // of Sources on the fly, in case the data corruption is extremely severe.
    if (m_num_unexid_frames.count(src_id) == 0) {
      m_num_unexid_frames[src_id] = 0;
    }
    m_num_unexid_frames[src_id]++;
  }
}

} // namespace dpdklibs
} // namespace dunedaq

// 
#include "detail/IfaceWrapper.hxx"
