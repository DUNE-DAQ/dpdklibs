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

IfaceWrapper::IfaceWrapper(uint16_t iface_id, source_to_sink_map_t& sources, std::atomic<bool>& run_marker)
    : m_iface_id(iface_id)
    , m_configured(false)
    , m_with_flow(false)
    , m_prom_mode(false)
    , m_ip_addr("")
    , m_mac_addr("")
    , m_socket_id(-1)
    , m_mtu(0)
    , m_rx_ring_size(0)
    , m_tx_ring_size(0)
    , m_num_mbufs(0)
    , m_burst_size(0)
    , m_mbuf_cache_size(0)
    , m_sources(sources)
    , m_run_marker(run_marker)
{ 
  m_iface_id_str = "iface-" + std::to_string(m_iface_id);
}

IfaceWrapper::~IfaceWrapper()
{
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << "IfaceWrapper destructor called. First stop check, then closing iface.";
  //graceful_stop();
  //close_iface();
  TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << "IfaceWrapper destroyed.";
}

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
  }

  std::stringstream ss;
  ss << "GARPMBP-" << m_iface_id;
  TLOG() << "Acquire GARP pool with name=" << ss.str() << " for iface_id=" << m_iface_id;
  m_garp_mbuf_pool = ealutils::get_mempool(ss.str());
  m_garp_bufs[0] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
  rte_pktmbuf_alloc_bulk(m_garp_mbuf_pool.get(), m_garp_bufs[0], m_burst_size);
}

void
IfaceWrapper::setup_interface()
{
  TLOG() << "Initialize interface " << m_iface_id;
  bool with_reset = true, with_mq_mode = false; // go to config
  bool check_link_status = false;

  int retval = ealutils::iface_init(m_iface_id, m_rx_qs.size(), m_tx_qs.size(), m_rx_ring_size, m_tx_ring_size, m_mbuf_pools, with_reset, with_mq_mode, check_link_status);
  if (retval != 0 ) {
    throw FailedToSetupInterface(ERS_HERE, m_iface_id, retval);
  }
  // Promiscuous mode
  ealutils::iface_promiscuous_mode(m_iface_id, m_prom_mode); // should come from config
}

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

// #warning RS FIXME -> Removed for conf overhaul
// //  if (m_cfg.with_drop_flow) {
//     // Adding drop flow
//     TLOG() << "Adding Drop Flow.";
//     flow = generate_drop_flow(m_iface_id, &error);
//     if (not flow) { // ers::fatal
//       TLOG() << "Drop flow can't be created for interface!"
//              << " Error type: " << (unsigned)error.type
//              << " Message: " << error.message;
//       rte_exit(EXIT_FAILURE, "error in creating flow");
//     }
//   //}
}

void
IfaceWrapper::setup_xstats() 
{
  // Stats setup
  m_iface_xstats.setup(m_iface_id);
  m_iface_xstats.reset_counters();
}

void
IfaceWrapper::conf(const iface_conf_t& args)
{
  if (m_configured) {
    TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) << "Interface is already configured! Won't touch it.";
  } else {
    // Load config
    m_cfg = args;
    m_with_flow = m_cfg.with_flow_control;
    m_prom_mode = m_cfg.promiscuous_mode;
    m_ip_addr = m_cfg.ip_addr;
    IpAddr ip_addr_struct(m_ip_addr);
    m_ip_addr_bin = udp::ip_address_dotdecimal_to_binary(
      ip_addr_struct.addr_bytes[3],
      ip_addr_struct.addr_bytes[2],
      ip_addr_struct.addr_bytes[1],
      ip_addr_struct.addr_bytes[0]
    );
    m_mac_addr = m_cfg.mac_addr;
    m_mbuf_cache_size = m_cfg.mbuf_cache_size;
    m_burst_size = m_cfg.burst_size;
    m_lcore_sleep_ns = m_cfg.lcore_sleep_us*1'000;
    m_mtu = m_cfg.mtu;
    m_num_mbufs = m_cfg.num_mbufs;
    m_rx_ring_size = m_cfg.rx_ring_size;
    m_tx_ring_size = m_cfg.tx_ring_size;

    // Get NUMA/Socket of interface
    m_socket_id = rte_eth_dev_socket_id(m_iface_id);
    TLOG() << "NUMA/Socket ID of iface[" << m_iface_id << "] is node=" << m_socket_id;
 
    for (const auto& exp_src : m_cfg.expected_sources) {
      if (m_ips.count(exp_src.ip_addr) != 0) {
        TLOG() << "Duplicate IP address as expected source under id=" << exp_src.id << "! Omitting source!";
        continue;
      } else {
        auto src_ip = exp_src.ip_addr;
        auto rx_q = exp_src.rx_q;
        auto lcore = exp_src.lcore;
        m_ips.insert(src_ip);
        m_rx_qs.insert(rx_q);
        m_lcores.insert(lcore);
    
        m_num_frames_rxq[rx_q] = { 0 };
        m_num_bytes_rxq[rx_q] = { 0 };
        m_num_frames_rxq_rejected[rx_q] = { 0 };
        m_num_bytes_rxq[rx_q] = { 0 };
        m_num_full_bursts[rx_q] = { 0 };
        m_max_burst_size[rx_q] = { 0 };

        m_num_frames_processed[rx_q] = { 0 };
        m_num_bytes_processed[rx_q] = { 0 };

        // No sanity check on config?    
        m_rx_core_map[lcore][rx_q] = src_ip;
        // Create a mbuf spsc queue (1/2 the mempool size)
        m_mbuf_queues_map[rx_q] = std::make_unique<mbuf_ptr_queue_t>(m_num_mbufs/2);

        // Check streams mapping and available source_ids
        auto& src_streams_map = exp_src.src_streams_mapping;
        for (const auto& src_stream_cfg : src_streams_map) {
          m_stream_to_source_id[rx_q][src_stream_cfg.stream_id] = src_stream_cfg.source_id;
          if (m_sources.count(src_stream_cfg.source_id) == 0) {
            //ers::fatal
            TLOG() << "Sink for source_id not initialized! source_id=" << src_stream_cfg.source_id;
          } else { 
            TLOG() << "Sink identified for rx_q=" << rx_q
                   << " stream_id=" << src_stream_cfg.stream_id 
                   << " source_id=" << src_stream_cfg.source_id;
          }
        }  
      }
    }

    // Log mapping
    for (auto const& [lcore, rx_qs] : m_rx_core_map) {
      TLOG() << "Lcore=" << lcore << " handles: ";
      for (auto const& [rx_q, src_ip] : rx_qs) {
        TLOG() << " rx_q=" << rx_q << " src_ip=" << src_ip;
      }
    }

    for( auto lcore : m_lcores) {
      TLOG() << "Registered LCore" << lcore;
    }

    for( auto q : m_rx_qs) {
      TLOG() << "Registered Queues" << q;
    }

    // Adding single TX queue for ARP responses
    TLOG() << "Append TX_Q=0 for ARP responses.";
    m_tx_qs.insert(0);


  }
}

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
    // int ret = rte_eal_remote_launch((int (*)(void*))(&IfaceWrapper::rx_runner), this, lcoreid);
    int ret = rte_eal_remote_launch((int (*)(void*))(&IfaceWrapper::rx_receiver), this, lcoreid);
    TLOG() << "  -> LCore[" << lcoreid << "] launched with return code=" << ret;
    int ret2 = rte_eal_remote_launch((int (*)(void*))(&IfaceWrapper::rx_router), this, lcoreid-m_core_offset);
    TLOG() << "  -> LCore[" << lcoreid-1<< "] launched with return code=" << ret2;
  }
}

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

void
IfaceWrapper::scrap()
{
  struct rte_flow_error error;
  rte_flow_flush(m_iface_id, &error);
}

void 
IfaceWrapper::get_info(opmonlib::InfoCollector& ci, int level)
{
  // Empty stat JSON placeholder
  nlohmann::json stat_json;

  nicreaderinfo::EthStats nr_eth_stats;
  nr_eth_stats.ipackets = m_iface_xstats.m_eth_stats.ipackets;
  nr_eth_stats.opackets = m_iface_xstats.m_eth_stats.opackets;
  nr_eth_stats.ibytes = m_iface_xstats.m_eth_stats.ibytes;
  nr_eth_stats.obytes = m_iface_xstats.m_eth_stats.obytes;
  nr_eth_stats.imissed = m_iface_xstats.m_eth_stats.imissed;
  nr_eth_stats.ierrors = m_iface_xstats.m_eth_stats.ierrors;
  nr_eth_stats.oerrors = m_iface_xstats.m_eth_stats.oerrors;
  nr_eth_stats.rx_nombuf = m_iface_xstats.m_eth_stats.rx_nombuf;
  ci.add(nr_eth_stats);

  // Poll stats from HW
  m_iface_xstats.poll();

  // Build JSON from values 
  for (int i = 0; i < m_iface_xstats.m_len; ++i) {
    stat_json[m_iface_xstats.m_xstats_names[i].name] = m_iface_xstats.m_xstats_values[i];
  }

  // Reset HW counters
  m_iface_xstats.reset_counters();

  // Convert JSON to NICReaderInfo struct
  nicreaderinfo::EthXStats nr_eth_xstats;
  nicreaderinfo::from_json(stat_json, nr_eth_xstats);
  // Push to InfoCollector
  ci.add(nr_eth_xstats);
  TLOG_DEBUG(TLVL_WORK_STEPS) << "opmonlib::InfoCollector object passed by reference to IfaceWrapper::get_info"
    << " -> Result looks like the following:\n" << ci.get_collected_infos();




  for( const auto& [src_rx_q,_] : m_num_frames_rxq) {
    nicreaderinfo::QueueStats qs;

    qs.packets_received = m_num_frames_rxq[src_rx_q].load();
    qs.bytes_received = m_num_bytes_rxq[src_rx_q].load();
    qs.packets_dropped_spsc_full = m_num_frames_rxq_rejected[src_rx_q].load();
    qs.spsc_queue_occupancy = m_mbuf_queues_map[src_rx_q]->sizeGuess();

    qs.full_rx_burst = m_num_full_bursts[src_rx_q].load();
    qs.max_burst_size = m_max_burst_size[src_rx_q].exchange(0);

    qs.packets_copied = m_num_frames_processed[src_rx_q].load();
    qs.bytes_copied = m_num_bytes_processed[src_rx_q].load();


    TLOG() << "XXXX pkt_rec=" << qs.packets_received << ", pkt_drop_spsc=" << qs.packets_dropped_spsc_full << ", pkt_copied=" << qs.packets_copied << ", pkt_queue_occ=" << qs.spsc_queue_occupancy;

    opmonlib::InfoCollector queue_ci;
    queue_ci.add(qs);

    ci.add(fmt::format("queue_{}", src_rx_q), queue_ci);
  }

  for( const auto& [src_id, src_obj] :  m_sources) {
    opmonlib::InfoCollector src_ci;
    src_obj->get_info(src_ci, level);
    ci.add(fmt::format("src_{}", src_id), src_ci);
  }
}

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

void
IfaceWrapper::handle_eth_payload(int src_rx_q, char* payload, std::size_t size)
{  
  // Get DAQ Header and its StreamID
  auto* daq_header = reinterpret_cast<dunedaq::detdataformats::DAQEthHeader*>(payload);
  auto src_id = m_stream_to_source_id[src_rx_q][(unsigned)daq_header->stream_id];

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
