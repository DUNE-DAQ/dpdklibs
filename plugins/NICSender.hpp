/**
 * @file NICSender.hpp Generic NIC sender DAQ Module over DPDK.
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef DPDKLIBS_PLUGINS_NICSENDER_HPP_
#define DPDKLIBS_PLUGINS_NICSENDER_HPP_

#include <ers/Issue.hpp>

#include <memory>
#include <string>
#include <vector>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include "appfwk/DAQModule.hpp"
#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"

#include "dpdklibs/nicsender/Nljs.hpp"
#include "dpdklibs/nicsender/Structs.hpp"

#include "dpdklibs/udp/PacketCtor.hpp"

namespace dunedaq {
namespace dpdklibs {

class NICSender : public dunedaq::appfwk::DAQModule
{
public:
  explicit NICSender(const std::string& name);
  ~NICSender();

  NICSender(const NICSender&) = delete;            ///< NICSender is not copy-constructible
  NICSender& operator=(const NICSender&) = delete; ///< NICSender is not copy-assignable
  NICSender(NICSender&&) = delete;                 ///< NICSender is not move-constructible
  NICSender& operator=(NICSender&&) = delete;      ///< NICSender is not move-assignable

  void init(const nlohmann::json& iniobj) override;

  // Map Core ID (LID) -> IP
  std::map<int, std::vector<std::string>> m_core_map;
  // MAP Core ID (LID) -> IP (as uint32)
  std::map<int, std::vector<uint32_t>> m_core_map32;
  std::atomic<bool> m_run_mark;

  int m_number_of_ips_per_core;
  int m_burst_size;

private:
  using module_conf_t = dunedaq::dpdklibs::nicsender::Conf;

  void do_configure(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);
  void do_scrap(const data_t&);
  void get_info(opmonlib::InfoCollector& ci, int level);

  void dpdk_configure();

  // template<typename T> int lcore_main(void *arg);

  void do_work(std::atomic<bool>&);

  int m_number_of_cores;
  int m_time_tick_difference;
  double m_rate;
  std::string m_frontend_type;
};

} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_PLUGINS_NICSENDER_HPP_
