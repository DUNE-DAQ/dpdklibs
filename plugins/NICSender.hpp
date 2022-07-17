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

#include "appfwk/DAQModule.hpp"
#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"

#include "dpdklibs/nicsender/Nljs.hpp"
#include "dpdklibs/nicsender/Structs.hpp"

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include "dpdklibs/udp/PacketCtor.hpp"

namespace dunedaq {
namespace dpdklibs {

class NICSender : public dunedaq::appfwk::DAQModule
{
public:
  explicit NICSender(const std::string& name);
  ~NICSender();

  NICSender(const NICSender&) =
    delete; ///< NICSender is not copy-constructible
  NICSender& operator=(const NICSender&) =
    delete; ///< NICSender is not copy-assignable
  NICSender(NICSender&&) =
    delete; ///< NICSender is not move-constructible
  NICSender& operator=(NICSender&&) =
    delete; ///< NICSender is not move-assignable

  void init(const nlohmann::json& iniobj) override;

private:
  // Types
  using module_conf_t = dunedaq::dpdklibs::nicsender::Conf;

  // Commands
  void do_start(const nlohmann::json& obj); 
  void do_stop(const nlohmann::json& obj);
  void do_configure(const nlohmann::json& obj);
  void do_scrap(const nlohmann::json& obj);
  void get_info(opmonlib::InfoCollector& ci, int level);


  // Threading
  void do_work(std::atomic<bool>&);

  std::atomic<bool> m_run_mark;

  int m_burst_size;
  int m_number_of_cores;
  double m_rate;

  // Configuration
};
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_PLUGINS_NICSENDER_HPP_
