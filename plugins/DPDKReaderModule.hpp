/**
 * @file DPDKReaderModule.hpp Generic NIC receiver DAQ Module over DPDK.
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

#include "datahandlinglibs/utils/ReusableThread.hpp"

//#include "dpdklibs/nicreader/Structs.hpp"
#include "dpdklibs/EALSetup.hpp"
#include "IfaceWrapper.hpp"

#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dunedaq::dpdklibs {

class DPDKReaderModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief DPDKReaderModule Constructor
   * @param name Instance name for this DPDKReaderModule instance
   */
  explicit DPDKReaderModule(const std::string& name);
  ~DPDKReaderModule();

  DPDKReaderModule(const DPDKReaderModule&) = delete;            ///< DPDKReaderModule is not copy-constructible
  DPDKReaderModule& operator=(const DPDKReaderModule&) = delete; ///< DPDKReaderModule is not copy-assignable
  DPDKReaderModule(DPDKReaderModule&&) = delete;                 ///< DPDKReaderModule is not move-constructible
  DPDKReaderModule& operator=(DPDKReaderModule&&) = delete;      ///< DPDKReaderModule is not move-assignable

  void init(const std::shared_ptr<appfwk::ModuleConfiguration> mfcg) override;


  
private:
  // Types
  //using module_conf_t = dunedaq::dpdklibs::nicreader::Conf;

  // Commands
  void do_configure(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);
  void do_scrap(const data_t&);

  // Internals
  std::shared_ptr<appfwk::ModuleConfiguration> m_cfg;
  
  int m_running = 0;
  std::atomic<bool> m_run_marker;
  void set_running(bool /*should_run*/);

  // Interfaces (logical ID, MAC) -> IfaceWrapper
  std::map<std::string, uint16_t> m_mac_to_id_map;
  std::map<std::string, uint16_t> m_pci_to_id_map;
  std::map<uint16_t, std::shared_ptr<IfaceWrapper>> m_ifaces;

  // Sinks (SourceConcepts)
  using sid_to_source_map_t = std::map<int, std::shared_ptr<SourceConcept>>;
  sid_to_source_map_t m_sources;

  // Comment of Monitoring
  // Both SourceConcepts and IfaceWrappers are Monitorable Objecets
  // Both quantities are available for the ReaderModule and both are registered.
  // There is no loop because the Sources passed to the Wrappers are not registered in the wrapper

  
};

} // namespace dunedaq::dpdklibs


#endif // DPDKLIBS_PLUGINS_NICRECEIVER_HPP_
