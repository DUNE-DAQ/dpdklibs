/**
 * @file NICReceiver.hpp Generic NIC receiver DAQ Module over DPDK.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
#define DPDKLIBS_PLUGINS_NICRECEIVER_HPP_

#include "dpdklibs/udp/Utils.hpp"

#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "appfwk/cmd/Structs.hpp"

#include "appfwk/DAQModule.hpp"
#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"

#include "readoutlibs/utils/ReusableThread.hpp"

#include "dpdklibs/nicreader/Structs.hpp"
#include "dpdklibs/nicreaderinfo/InfoNljs.hpp"
#include "dpdklibs/EALSetup.hpp"
#include "IfaceWrapper.hpp"

#include <folly/ProducerConsumerQueue.h>

#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq::dpdklibs {

class NICReceiver : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief NICReceiver Constructor
   * @param name Instance name for this NICReceiver instance
   */
  explicit NICReceiver(const std::string& name);
  ~NICReceiver();

  NICReceiver(const NICReceiver&) = delete;            ///< NICReceiver is not copy-constructible
  NICReceiver& operator=(const NICReceiver&) = delete; ///< NICReceiver is not copy-assignable
  NICReceiver(NICReceiver&&) = delete;                 ///< NICReceiver is not move-constructible
  NICReceiver& operator=(NICReceiver&&) = delete;      ///< NICReceiver is not move-assignable

  void init(const data_t& args) override;

private:
  // Types
  using module_conf_t = dunedaq::dpdklibs::nicreader::Conf;

  // Commands
  void do_configure(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);
  void do_scrap(const data_t&);
  void get_info(opmonlib::InfoCollector& ci, int level);

  // Internals
  int m_running = 0;
  std::atomic<bool> m_run_marker;
  void set_running(bool /*should_run*/);

  // Configuration
  module_conf_t m_cfg;
  std::string m_dest_ip;
  int m_num_ip_sources;
  int m_num_rx_cores;
  std::set<int> m_rx_qs;
  std::map<int, std::map<int, std::string>> m_rx_core_map;

  // Routing policy
  std::string m_routing_policy;
  int m_prev_sink;
  int m_next_sink; 

  // What to do with every payload
  void handle_eth_payload(int src_rx_q, char* payload, std::size_t size);

  // Stats
  int m_burst_number = 0;
  int m_sum = 0;
  std::map<int, std::atomic<std::size_t>> m_num_frames;
  std::map<int, std::atomic<std::size_t>> m_num_bytes;
  std::map<int, std::atomic<std::size_t>> m_num_unexid_frames;
  std::thread m_stat_thread;

  // DPDK
  unsigned m_num_ifaces;
  uint16_t m_iface_id;
  volatile uint8_t m_dpdk_quit_signal;
  const int m_burst_size = 256;
  std::map<int, std::unique_ptr<rte_mempool>> m_mbuf_pools;
  std::map<int, struct rte_mbuf **> m_bufs;

  // Lcore processor
  //template<class T> 
  int rx_runner(void *arg __rte_unused);

  // Interfaces (logical ID, MAC) -> IfaceWrapper
  std::map<std::string, uint16_t> m_mac_to_id_map;
  std::map<uint16_t, std::unique_ptr<IfaceWrapper>> m_ifaces;

  // Sinks (SourceConcepts)
  using source_to_sink_map_t = std::map<int, std::unique_ptr<SourceConcept>>;
  source_to_sink_map_t m_sources;

  // Opmon
  std::atomic<int> m_total_groups_sent {0};
  std::atomic<int> m_groups_sent {0};

  std::unique_ptr<udp::PacketInfoAccumulator> m_accum_ptr;
  bool m_per_stream_reports = true;
};

} // namespace dunedaq::dpdklibs

#include "detail/NICReceiver.hxx"

#endif // DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
