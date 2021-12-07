/**
 * @file NICReader.hpp Generic NIC reader DAQ Module over DPDK.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef FLXLIBS_PLUGINS_NICREADER_HPP_
#define FLXLIBS_PLUGINS_NICREADER_HPP_

#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "appfwk/cmd/Structs.hpp"

#include "dpdklibs/nicreader/Structs.hpp"

// From appfwk
#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSink.hpp"

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>

#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq::dpdklibs {

class NICReader : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief NICReader Constructor
   * @param name Instance name for this NICReader instance
   */
  explicit NICReader(const std::string& name);

  NICReader(const NICReader&) = delete;            ///< NICReader is not copy-constructible
  NICReader& operator=(const NICReader&) = delete; ///< NICReader is not copy-assignable
  NICReader(NICReader&&) = delete;                 ///< NICReader is not move-constructible
  NICReader& operator=(NICReader&&) = delete;      ///< NICReader is not move-assignable

  void init(const data_t& args) override;

private:
  // Types
  using module_conf_t = dunedaq::dpdklibs::nicreader::Conf;

  // Commands
  void do_configure(const data_t& args);
  void do_start(const data_t& args);
  void do_stop(const data_t& args);
  void get_info(opmonlib::InfoCollector& ci, int level);

  // Internals
  bool m_running = false;

};

} // namespace dunedaq::dpdklibs

#endif // FLXLIBS_PLUGINS_NICREADER_HPP_
