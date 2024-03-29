/**
 * @file IfaceWrapper.hpp IfaceWrapper for holding resources of 
 * a DPDK controlled NIC interface/port
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_IFACEWRAPPER_HPP_
#define DPDKLIBS_SRC_IFACEWRAPPER_HPP_

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/arp/ARP.hpp"
#include "dpdklibs/ipv4_addr.hpp"
#include "dpdklibs/XstatsHelper.hpp"
#include "SourceConcept.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <set>

#include "utilities/ReusableThread.hpp"
#include <folly/ProducerConsumerQueue.h>

namespace dunedaq {
namespace dpdklibs {

class IfaceWrapper
{
public:
  using iface_conf_t = dunedaq::dpdklibs::nicreader::Interface;
  using source_conf_t = dunedaq::dpdklibs::nicreader::Source;
  using source_to_sink_map_t = std::map<int, std::unique_ptr<SourceConcept>>;

  IfaceWrapper(uint16_t iface_id, source_to_sink_map_t& sources, std::atomic<bool>& run_marker);
  ~IfaceWrapper(); 
 
  IfaceWrapper(const IfaceWrapper&) = delete;            ///< IfaceWrapper is not copy-constructible
  IfaceWrapper& operator=(const IfaceWrapper&) = delete; ///< IfaceWrapper is not copy-assginable
  IfaceWrapper(IfaceWrapper&&) = delete;                 ///< IfaceWrapper is not move-constructible
  IfaceWrapper& operator=(IfaceWrapper&&) = delete;      ///< IfaceWrapper is not move-assignable

  void init(const iface_conf_t& args);
  void conf(const iface_conf_t& args);
  void start();
  void stop();
  void scrap();
  void get_info(opmonlib::InfoCollector& ci, int level);

  void allocate_mbufs();
  void setup_interface();
  void setup_flow_steering();
  void setup_xstats();

  void enable_flow() { m_lcore_enable_flow.store(true);}
  void disable_flow() { m_lcore_enable_flow.store(false);}
  
protected:
  iface_conf_t m_cfg;
  int m_iface_id;
  std::string m_iface_id_str;
  bool m_configured;

  bool m_with_flow;
  bool m_prom_mode;
  std::string m_ip_addr;
  rte_be32_t m_ip_addr_bin;
  std::string m_mac_addr;
  int m_socket_id;
  int m_mtu;
  uint16_t m_rx_ring_size;
  uint16_t m_tx_ring_size;
  int m_num_mbufs;
  int m_burst_size;
  uint32_t m_lcore_sleep_ns;
  int m_mbuf_cache_size;

private:
  int m_num_ip_sources;
  int m_num_rx_cores;
  std::set<std::string> m_ips;
  std::set<int> m_rx_qs;
  std::set<int> m_tx_qs;
  std::set<int> m_lcores;
  std::map<int, std::map<int, std::string>> m_rx_core_map;

  // Lcore stop signal
  std::atomic<bool> m_lcore_quit_signal{ false };

  std::atomic<bool> m_lcore_enable_flow{ false };

  // Mbufs and pools
  std::map<int, std::unique_ptr<rte_mempool>> m_mbuf_pools;
  std::map<int, struct rte_mbuf **> m_bufs; // by queue

  // Stats by queues
  std::map<int, std::atomic<std::size_t>> m_num_frames_rxq;
  std::map<int, std::atomic<std::size_t>> m_num_bytes_rxq;
  std::map<int, std::atomic<std::size_t>> m_num_unexid_frames;
  std::map<int, std::atomic<std::size_t>> m_num_full_bursts;
  std::map<int, std::atomic<uint16_t>> m_max_burst_size;

  // DPDK HW stats
  dpdklibs::IfaceXstats m_iface_xstats;

  // Source to sink map
  std::map<int, std::map<int, int>> m_stream_to_source_id;
  source_to_sink_map_t& m_sources;

  // Run marker
  std::atomic<bool>& m_run_marker;

  // GARP
  std::unique_ptr<rte_mempool> m_garp_mbuf_pool;
  std::map<int, struct rte_mbuf **> m_garp_bufs;
  std::thread m_garp_thread;
  void garp_func();
  std::atomic<uint64_t> m_garps_sent{0};
  
  // Lcore processor
  //template<class T> 
  int rx_runner(void *arg __rte_unused);

  // What to do with every payload
  void handle_eth_payload(int src_rx_q, char* payload, std::size_t size);

};

} // namespace dpdklibs
} // namespace dunedaq

// #include "detail/IfaceWrapper.hxx"

#endif // DPDKLIBS_SRC_IFACEWRAPPER_HPP_
