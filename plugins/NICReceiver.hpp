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

#include "dpdklibs/nicreader/Structs.hpp"
#include "dpdklibs/EALSetup.hpp"

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
  void do_configure(const data_t& args);
  void do_start(const data_t& args);
  void do_stop(const data_t& args);
  void do_scrap(const data_t& args);
  void get_info(opmonlib::InfoCollector& ci, int level);

  // Internals
  int m_running = 0;
  module_conf_t m_cfg;
  std::atomic<bool> m_run_marker;
  void set_running(bool /*should_run*/);

  // Stats
  int m_burst_number = 0;
  int m_sum = 0;
  std::atomic<int> m_num_frames;
  std::thread m_stat_thread;

  // DPDK
  const int m_burst_size = 512; 
  std::unique_ptr<rte_mempool> m_mbuf_pool;
  struct rte_mbuf **m_bufs;
  unsigned m_nb_ports;
  uint16_t m_portid;

  template<class T> 
  int rx_runner();

};

} // namespace dunedaq::dpdklibs

// Declarations
#include "detail/NICReceiver.hxx"

#endif // DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
