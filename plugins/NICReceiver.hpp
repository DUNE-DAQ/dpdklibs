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

#include "dpdklibs/nicreader/Structs.hpp"

// From appfwk
#include "appfwk/DAQModule.hpp"

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

};

} // namespace dunedaq::dpdklibs

#endif // DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
