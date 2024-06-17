/**
 * @file DPDKReaderModule.cpp DPDKReaderModule DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
//#include "dpdklibs/nicreader/Nljs.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appfwk/ModuleConfiguration.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"

#include "appmodel/DataReceiverModule.hpp"
#include "appmodel/DPDKReaderConf.hpp"
#include "confmodel/NetworkDevice.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "logging/Logging.hpp"

#include "readoutlibs/ReadoutIssues.hpp"
#include "readoutlibs/utils/BufferCopy.hpp" 

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/RTEIfaceSetup.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/FlowControl.hpp"
#include "dpdklibs/receiverinfo/InfoNljs.hpp"
#include "CreateSource.hpp"
#include "DPDKReaderModule.hpp"

#include "opmonlib/InfoCollector.hpp"

#include <cinttypes>
#include <chrono>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <ios>

/**
 * @brief Name used by TRACE TLOG calls from this source file
 */
#define TRACE_NAME "DPDKReaderModule" // NOLINT

/**
 * @brief TRACE debug levels used in this source file
 */
enum
{
  TLVL_ENTER_EXIT_METHODS = 5,
  TLVL_WORK_STEPS = 10,
  TLVL_BOOKKEEPING = 15
};

namespace dunedaq {
namespace dpdklibs {

DPDKReaderModule::DPDKReaderModule(const std::string& name)
  : DAQModule(name),
    m_run_marker{ false }
{
  register_command("conf", &DPDKReaderModule::do_configure);
  register_command("start", &DPDKReaderModule::do_start);
  register_command("stop_trigger_sources", &DPDKReaderModule::do_stop);
  register_command("scrap", &DPDKReaderModule::do_scrap);
}

DPDKReaderModule::~DPDKReaderModule()
{
  TLOG() << get_name() << ": Destructor called. Tearing down EAL.";
  ealutils::finish_eal();
}

inline void
tokenize(std::string const& str, const char delim, std::vector<std::string>& out)
{
  std::size_t start;
  std::size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    out.push_back(str.substr(start, end - start));
  }
}

void
DPDKReaderModule::init(const std::shared_ptr<appfwk::ModuleConfiguration> mcfg )
{
 auto mdal = mcfg->module<appmodel::DataReceiverModule>(get_name());
 m_cfg = mcfg;
 if (mdal->get_outputs().empty()) {
	auto err = dunedaq::readoutlibs::InitializationError(ERS_HERE, "No outputs defined for NIC reader in configuration.");
  ers::fatal(err);
	throw err;
 }

 for (auto con : mdal->get_outputs()) {
  auto queue = con->cast<confmodel::QueueWithSourceId>();
  if(queue == nullptr) {
	  auto err = dunedaq::readoutlibs::InitializationError(ERS_HERE, "Outputs are not of type QueueWithGeoId.");
	  ers::fatal(err);
	  throw err;
  }

  // Check for CB prefix indicating Callback use
  const char delim = '_';
  std::string target = queue->UID();
  std::vector<std::string> words;
  tokenize(target, delim, words);
  int sourceid = -1;

  bool callback_mode = false;
  if (words.front() == "cb") {
    callback_mode = true;
  }

  m_sources[queue->get_source_id()] = createSourceModel(queue->UID(), callback_mode);
  //m_sources[queue->get_source_id()]->init(); 
 }
}

void
DPDKReaderModule::do_configure(const data_t& /*args*/)
{
  TLOG() << get_name() << ": Entering do_conf() method";
  //auto session = appfwk::ModuleManager::get()->session();
  auto mdal = m_cfg->module<appmodel::DataReceiverModule>(get_name());
  auto module_conf = mdal->get_configuration()->cast<appmodel::DPDKReaderConf>();
  auto res_set = mdal->get_connections();
  // EAL setup
  TLOG() << "Setting up EAL with params from config.";
  std::vector<std::string> eal_params ;
  eal_params.push_back("eal_cmdline");
  eal_params.push_back("--proc-type=primary");

  // Construct the pcie devices allowed mask
  std::string first_pcie_addr;
  bool is_first_pcie_addr = true;
  std::vector<const confmodel::DetectorToDaqConnection*> d2d_conns;
  for (auto res : res_set) {
    auto connection = res->cast<confmodel::DetectorToDaqConnection>();
    if (connection == nullptr) {
      dunedaq::readoutlibs::GenericConfigurationError err(
          ERS_HERE, "DetectorToDaqConnection configuration failed due expected but unavailable connection!"
        );
      ers::fatal(err);
      throw err;      
    }
    if (connection->disabled(*(m_cfg->configuration_manager()->session()))) {
	    continue;
    }

    d2d_conns.push_back(connection);

    auto receiver = connection->get_receiver()->cast<appmodel::DPDKReceiver>();
    if (!receiver) {
      throw dunedaq::readoutlibs::InitializationError(
        ERS_HERE, fmt::format("Found {} of type {} in connection {} while expecting type DPDKReceiver", receiver->class_name(), receiver->UID(), connection->UID())
      );
    }

    auto net_device = receiver->get_uses()->cast<confmodel::NetworkDevice>();

    if (is_first_pcie_addr) {
      first_pcie_addr = net_device->get_pcie_addr();
      is_first_pcie_addr = false;
    }
    eal_params.push_back("-a");
    eal_params.push_back(net_device->get_pcie_addr());
  }


  // Use the first pcie device id as file prefix
  // FIXME: Review this strategy - should work in most of cases, but it could be 
  // confusing in configs with multiple connections
  eal_params.push_back(fmt::format("--file-prefix={}", first_pcie_addr));

  eal_params.push_back(module_conf->get_eal_args());

  ealutils::init_eal(eal_params);

  // Get available connections from EAL
  auto available_ifaces = ifaceutils::get_num_available_ifaces();
  TLOG() << "Number of available connections: " << available_ifaces;
  for (unsigned int ifc_id=0; ifc_id<available_ifaces; ++ifc_id) {
    std::string mac_addr_str = ifaceutils::get_iface_mac_str(ifc_id);
    std::string pci_addr_str = ifaceutils::get_iface_pci_str(ifc_id);
    m_mac_to_id_map[mac_addr_str] = ifc_id;
    // TODO: remove
    m_pci_to_id_map[pci_addr_str] = ifc_id;
    TLOG() << "Available iface with MAC=" << mac_addr_str << " PCIe=" <<  pci_addr_str << " logical ID=" << ifc_id;
  }

  for (auto d2d_conn : d2d_conns) {
    auto dpdk_receiver = d2d_conn->get_receiver()->cast<appmodel::DPDKReceiver>();
    auto senders = d2d_conn->get_senders();
    std::vector<const appmodel::NWDetDataSender*> nw_senders;
    for ( auto sender : d2d_conn->get_senders() ) {
      auto nw_sender = sender->cast<appmodel::NWDetDataSender>();
      if ( !nw_sender ) {
        throw dunedaq::readoutlibs::InitializationError(
          ERS_HERE, fmt::format("Found {} of type {} in connection {} while expecting type NWDetDataSender", dpdk_receiver->class_name(), dpdk_receiver->UID(), d2d_conn->UID())
        );
      }

      if ( nw_sender->disabled(*(m_cfg->configuration_manager()->session())) ) {
        continue;
      }
      nw_senders.push_back(nw_sender);
    }

    auto net_device = dpdk_receiver->get_uses()->cast<confmodel::NetworkDevice>();
    
    if ((m_mac_to_id_map.count(net_device->get_mac_address()) == 0) || (m_pci_to_id_map.count(net_device->get_pcie_addr()) == 0)) {
        TLOG() << "No available interface with MAC=" << net_device->get_mac_address();
        throw dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "DPDKReaderModule configuration failed due expected but unavailable interface!"
        );
    }
    
    uint iface_id = m_mac_to_id_map[net_device->get_mac_address()];
    m_ifaces[iface_id] = std::make_unique<IfaceWrapper>(iface_id, dpdk_receiver, nw_senders,  m_sources, m_run_marker); 
    m_ifaces[iface_id]->allocate_mbufs();
    m_ifaces[iface_id]->setup_interface();
    m_ifaces[iface_id]->setup_flow_steering();
    m_ifaces[iface_id]->setup_xstats();

  }

  if (!m_run_marker.load()) {
    set_running(true);
    TLOG() << "Starting iface wrappers.";
    for (auto& [iface_id, iface] : m_ifaces) {
      iface->start();
    }
  } else {
    TLOG_DEBUG(5) << "iface wrappers are already running!";
  }

  return;

}

void
DPDKReaderModule::do_start(const data_t&)
{

  // Setup callbacks on all sourcemodels
  for (auto& [sourceid, source] : m_sources) {
    source->acquire_callback();
  }

  for (auto& [iface_id, iface] : m_ifaces) {
    iface->enable_flow();
  }
}

void
DPDKReaderModule::do_stop(const data_t&)
{
  for (auto& [iface_id, iface] : m_ifaces) {
    iface->disable_flow();
  }
}


void
DPDKReaderModule::do_scrap(const data_t&)
{
  TLOG() << get_name() << ": Entering do_scrap() method";
  if (m_run_marker.load()) {
    TLOG() << "Raising stop through variables!";
    set_running(false);
    TLOG() << "Stopping iface wrappers.";
    for (auto& [iface_id, iface] : m_ifaces) {
      iface->stop();
    }
    ealutils::wait_for_lcores();
    TLOG() << "Stoppped DPDK lcore processors and internal threads...";
  } else {
    TLOG_DEBUG(5) << "DPDK lcore processor is already stopped!";
  }
}

void
DPDKReaderModule::get_info(opmonlib::InfoCollector& ci, int level)
{
  for (auto& [iface_id, iface] : m_ifaces) {
    iface->get_info(ci, level);
  } 
}

void 
DPDKReaderModule::set_running(bool should_run)
{
  bool was_running = m_run_marker.exchange(should_run);
  TLOG_DEBUG(5) << "Active state was toggled from " << was_running << " to " << should_run;
}

} // namespace dpdklibs
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::DPDKReaderModule)
