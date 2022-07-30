/**
 * @file NICReceiver.hpp Generic NIC receiver DAQ Module over DPDK.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
#define DPDKLIBS_PLUGINS_NICRECEIVER_HPP_

#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "appfwk/cmd/Structs.hpp"

#include "appfwk/DAQModule.hpp"
#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"

#include "readoutlibs/utils/ReusableThread.hpp"
#include "detdataformats/tde/TDE16Frame.hpp"

#include "dpdklibs/nicreader/Structs.hpp"
#include "dpdklibs/EALSetup.hpp"

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
  using amc_frame_queue_t = folly::ProducerConsumerQueue<detdataformats::tde::TDE16Frame>;
  using amc_frame_queue_ptr_t = std::unique_ptr<amc_frame_queue_t>;

  // Commands
  void do_configure(const data_t& args);
  void do_start(const data_t& args);
  void do_stop(const data_t& args);
  void do_scrap(const data_t& args);
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

  // TDE specifics
  inline static const std::string m_parser_thread_name = "ipp";
  inline static const std::size_t m_amc_queue_capacity = 1000;
  std::map<int, amc_frame_queue_ptr_t> m_amc_data_queues;
  std::map<int, std::unique_ptr<readoutlibs::ReusableThread>> m_amc_frame_handlers;
  //std::map<int, std::atomic<uint64_t>> m_amc_frame_dropped;
  void handle_frame_queue(int id);
  void copy_out(int queue, char* message, std::size_t size);

  // Stats
  int m_burst_number = 0;
  int m_sum = 0;
  std::map<int, std::atomic<int>> m_num_frames;
  
  std::atomic<int> m_cleaned;
  std::thread m_stat_thread;

  // DPDK
  const int m_burst_size = 512; 
  std::map<int, std::unique_ptr<rte_mempool>> m_mbuf_pools;
  std::map<int, struct rte_mbuf **> m_bufs;
  //struct rte_mbuf **m_bufs;
  unsigned m_nb_ports;
  uint16_t m_portid;
  volatile uint8_t m_dpdk_quit_signal;

  // Lcore processor
  //template<class T> 
  int rx_runner(void *arg __rte_unused);

};

} // namespace dunedaq::dpdklibs

// Declarations
#include "detail/NICReceiver.hxx"

#endif // DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
