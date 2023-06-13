/**
 * @file IfaceWrapper.cpp DPDK based Interface wrapper
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "logging/Logging.hpp"
#include "readoutlibs/ReadoutIssues.hpp"

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/RTEIfaceSetup.hpp"
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
    m_mbuf_pools[i] = ealutils::get_mempool(ss.str());
    m_bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
    rte_pktmbuf_alloc_bulk(m_mbuf_pools[i].get(), m_bufs[i], m_burst_size);
  } 
}

void
IfaceWrapper::setup_interface()
{
  TLOG() << "Initialize interface " << m_iface_id;
  bool with_reset = true, with_mq_mode = true; // go to config
  ealutils::iface_init(m_iface_id, m_rx_qs.size(), m_tx_qs.size(), m_mbuf_pools, with_reset, with_mq_mode);
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

#warning RS FIXME -> Removed for conf overhaul
//  if (m_cfg.with_drop_flow) {
    // Adding drop flow
    TLOG() << "Adding Drop Flow.";
    flow = generate_drop_flow(m_iface_id, &error);
    if (not flow) { // ers::fatal
      TLOG() << "Drop flow can't be created for interface!"
             << " Error type: " << (unsigned)error.type
             << " Message: " << error.message;
      rte_exit(EXIT_FAILURE, "error in creating flow");
    }
  //}
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
    m_mac_addr = m_cfg.mac_addr;
    m_mbuf_cache_size = m_cfg.mbuf_cache_size;
    m_burst_size = m_cfg.burst_size;
    m_mtu = m_cfg.mtu;
    m_num_mbufs = m_cfg.num_mbufs;
    m_rx_ring_size = m_cfg.rx_ring_size;
    m_tx_ring_size = m_cfg.tx_ring_size;
 
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

        // No sanity check on config?    
        m_rx_core_map[lcore][rx_q] = src_ip;

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

    // Adding single TX queue for ARP responses
    TLOG() << "Append TX_Q=0 for ARP responses.";
    m_tx_qs.insert(0);

  }

}

void
IfaceWrapper::start()
{
  m_stat_thread = std::thread([&]() {
    TLOG() << "Launching stat thread of iface=" << m_iface_id;
    while (m_run_marker.load()) {
      for (auto& [qid, nframes] : m_num_frames_rxq) { // check for new frames
        if (nframes.load() > 0) {
          auto nbytes = m_num_bytes_rxq[qid].load();
          TLOG() << "Received payloads on iface=" << m_iface_id 
                 << " of q[" << qid << "] is: " << nframes.load()
                 << " Bytes: " << nbytes << " Rate: " << nbytes / 1e6 * 8 << " Mbps";
          nframes.exchange(0);
          m_num_bytes_rxq[qid].exchange(0);
        }
      }
      for (auto& [strid, nframes] : m_num_unexid_frames) { // check for unexpected StreamID frames
        if (nframes.load() > 0) {
          TLOG() << "Unexpected StreamID frames on iface= " << m_iface_id 
                 << " with strid[" << strid << "]! Num: " << nframes.load();
          nframes.exchange(0);
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  TLOG() << "Launching GARP thread with garp_func...";
  m_garp_thread = std::thread(&IfaceWrapper::garp_func, this);

  TLOG() << "Interface id=" << m_iface_id << " starting LCore processors:";
  for (auto const& [lcoreid, _] : m_rx_core_map) {
    int ret = rte_eal_remote_launch((int (*)(void*))(&IfaceWrapper::rx_runner), this, lcoreid);
    TLOG() << "  -> LCore[" << lcoreid << "] launched with return code=" << ret;
  }

}

void
IfaceWrapper::stop()
{
  if (m_stat_thread.joinable()) {
    m_stat_thread.join();
  } else {
    TLOG() << "Stats thread is not joinable!";
  }
  
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
IfaceWrapper::garp_func()
{
  std::unique_ptr<rte_mempool> garp_mbuf_pool;
  std::stringstream ss;
  ss << "GARPMBP-" << m_iface_id;
  TLOG() << "Acquire GARP pool with name=" << ss.str() << " for iface_id=" << m_iface_id;
  garp_mbuf_pool = ealutils::get_mempool(ss.str());
  struct rte_mbuf **garp_bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
  rte_pktmbuf_alloc_bulk(garp_mbuf_pool.get(), garp_bufs, m_burst_size);

  TLOG() << "Setting up GARP IP for iface with IP=" << m_ip_addr;
  IpAddr ip_addr_struct(m_ip_addr);
  rte_be32_t ip_addr_bin = udp::ip_address_dotdecimal_to_binary(
    ip_addr_struct.addr_bytes[3],
    ip_addr_struct.addr_bytes[2],
    ip_addr_struct.addr_bytes[1],
    ip_addr_struct.addr_bytes[0]
  );
  
  TLOG() << "Launching GARP sender...";
  while(m_run_marker.load()) {
    arp::pktgen_send_garp(garp_bufs[0], m_iface_id, ip_addr_bin);   
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
  
  if (m_sources.count(src_id) != 0) {
    auto ret = m_sources[src_id]->handle_payload(payload, size);
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
