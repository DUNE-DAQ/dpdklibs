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

//#include "appfwk/app/Nljs.hpp"
#include "appfwk/cmd/Nljs.hpp"
#include "appfwk/cmd/Structs.hpp"

#include "appfwk/DAQModule.hpp"

#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"

#include "readoutlibs/utils/ReusableThread.hpp"

//#include "dpdklibs/nicreader/Structs.hpp"
//#include "dpdklibs/nicreaderinfo/InfoNljs.hpp"
#include "dpdklibs/EALSetup.hpp"
#include "IfaceWrapper.hpp"

#include <future>
#include <map>
#include <memory>
#include <mutex>
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

  void init(const std::shared_ptr<appfwk::ModuleConfiguration> mfcg) override;

private:
  // Types
  //using module_conf_t = dunedaq::dpdklibs::nicreader::Conf;

  // Commands
  void do_configure(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);
  void do_scrap(const data_t&);
  void get_info(opmonlib::InfoCollector& ci, int level);

  // Internals
  std::shared_ptr<appfwk::ModuleConfiguration> m_cfg;
  
  int m_running = 0;
  std::atomic<bool> m_run_marker;
  void set_running(bool /*should_run*/);

  // Interfaces (logical ID, MAC) -> IfaceWrapper
  std::map<std::string, uint16_t> m_mac_to_id_map;
  std::map<uint16_t, std::unique_ptr<IfaceWrapper>> m_ifaces;

  // Sinks (SourceConcepts)
  using source_to_sink_map_t = std::map<int, std::unique_ptr<SourceConcept>>;
  source_to_sink_map_t m_sources;

};

} // namespace dunedaq::dpdklibs

// #include "detail/NICReceiver.hxx"

#endif // DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
