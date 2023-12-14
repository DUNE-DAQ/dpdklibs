/**
 * @file NICReceiver.cpp NICReceiver DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
//#include "dpdklibs/nicreader/Nljs.hpp"

#include <appfwk/ConfigurationManager.hpp>
#include <appfwk/ModuleConfiguration.hpp>

#include "appdal/NICReceiver.hpp"
#include "appdal/NICReceiverConf.hpp"
#include "appdal/NICInterface.hpp"
#include "appdal/NICStatsConf.hpp"
#include "appdal/EthStreamParameters.hpp"
#include "coredal/QueueWithGeoId.hpp"

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
#include "NICReceiver.hpp"

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
#define TRACE_NAME "NICReceiver" // NOLINT

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

NICReceiver::NICReceiver(const std::string& name)
  : DAQModule(name),
    m_run_marker{ false },
    m_routing_policy{ "incremental" },
    m_prev_sink(0),
    m_next_sink(0)
{
  register_command("conf", &NICReceiver::do_configure);
  register_command("start", &NICReceiver::do_start);
  register_command("stop", &NICReceiver::do_stop);
  register_command("scrap", &NICReceiver::do_scrap);
}

NICReceiver::~NICReceiver()
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
NICReceiver::init()
{
 auto mdal = appfwk::ModuleConfiguration::get()->module<appdal::NICReceiver>(get_name());
 if (mdal->get_outputs().empty()) {
	auto err = dunedaq::readoutlibs::InitializationError(ERS_HERE, "No outputs defined for NIC reader in configuration.");
  ers::fatal(err);
	throw err;
 }

 for (auto con : mdal->get_outputs()) {
  auto queue = con->cast<QueueWithGeoId>();
  if(queue == nullptr) {
	  auto err = dunedaq::readoutlibs::InitializationError(ERS_HERE, "Outputs are not of type QueueWithGeoId.");
	  ers::fatal(err);
	  throw err;
  }
  utils::StreamUID s;
  s.det_id = queue->get_geo_id().get_detector_id();
  s.crate_id = queue->get_geo_id().get_crate_id();
  s.slot_id = queue->get_geo_id().get_slot_id();
  s.stream_id = queue->get_geo_id().get_stream_id();

  m_sources[s] = createSourceModel(queue->UID());
  m_sources[s]->init(); 
 }
}

void
NICReceiver::do_configure()
{
  TLOG() << get_name() << ": Entering do_conf() method";
  auto session = appfwk::ModuleManager::get()->session();
  auto mdal = appfwk::ModuleConfiguration::get()->module<appdal::NICReceiver>(get_name());
  auto module_conf = mdal->get_configuration();
  auto ro_group = mdal->get_readout_group().cast<coredal::ReadoutGroup>;

  // EAL setup
  TLOG() << "Setting up EAL with params from config.";
  std::vector<char*> eal_params = ealutils::string_to_eal_args(module_conf->get_eal_args());
  ealutils::init_eal(eal_params.size(), eal_params.data());

  // Get available Interfaces from EAL
  auto available_ifaces = ifaceutils::get_num_available_ifaces();
  TLOG() << "Number of available interfaces: " << available_ifaces;
  for (unsigned int ifc_id=0; ifc_id<available_ifaces; ++ifc_id) {
    std::string mac_addr_str = ifaceutils::get_iface_mac_str(ifc_id);
    m_mac_to_id_map[mac_addr_str] = ifc_id;
    TLOG() << "Available iface with MAC=" << mac_addr_str << " logical ID=" << ifc_id;
  }

  auto res_set = ro_group->get_contains();
 
  for (auto res : res_set) {
    auto interface = res->cast<NICInterface>;
    if (interface == nullptr) {
      dunedaq::readoutlibs::ConfigurationError err(
          ERS_HERE, "NICReceiver configuration failed due expected but unavailable interface!");
      ers::fatal(err);
      throw err;      
    }
    if (interface->disabled(*session)) {
	    continue;
    }

    if (m_mac_to_id_map.find(interface->get_rx_mac()) != m_mac_to_id_map.end()) {
       m_mac_to_ip[interface->get_rx_mac()] = interface->get_rx_ip(); 
       m_ifaces[interface->get_rx_iface()] = std::make_unique<IfaceWrapper>(interface->UID(), m_sources, m_run_marker); 
       //m_ifaces[interface->get_rx_iface()] = conf(iface_cfg);
       m_ifaces[interface->get_rx_iface()] = allocate_mbufs();;
       m_ifaces[interface->get_rx_iface()] = setup_flow_steering();
       m_ifaces[interface->get_rx_iface()] = setup_xstats();
    } else {
       TLOG() << "No available interface with MAC=" << iface_mac_addr;
       ers::fatal(dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "NICReceiver configuration failed due expected but unavailable interface!"));
    }
  }
  
  return;

}

void
NICReceiver::do_start(const data_t&)
{
  TLOG() << get_name() << ": Entering do_start() method";
  if (!m_run_marker.load()) {
    set_running(true);
    TLOG() << "Starting iface wrappers.";
    for (auto& [iface_id, iface] : m_ifaces) {
      iface->start();
    }
  } else {
    TLOG_DEBUG(5) << "NICReader is already running!";
  }
}

void
NICReceiver::do_stop(const data_t&)
{
  TLOG() << get_name() << ": Entering do_stop() method";
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
  return;
}

void
NICReceiver::do_scrap(const data_t&)
{
  TLOG() << get_name() << ": Entering do_scrap() method";
  for (auto& [iface_id, iface] : m_ifaces) {
    iface->scrap();
  }
}

void
NICReceiver::get_info(opmonlib::InfoCollector& ci, int level)
{
  for (auto& [iface_id, iface] : m_ifaces) {
    iface->get_info(ci, level);
  } 
}

void
NICReceiver::handle_eth_payload(int src_rx_q, char* payload, std::size_t size) {
  // Get DAQ Header and its StreamID
  auto* daq_header = reinterpret_cast<dunedaq::detdataformats::DAQEthHeader*>(payload);
  //auto strid = (unsigned)daq_header->stream_id;
  auto strid = (unsigned)daq_header->stream_id+(daq_header->slot_id<<8)+(daq_header->crate_id<<(8+4))+(daq_header->det_id<<(8+4+10));
  if (m_sources.count(strid) != 0) {
    auto ret = m_sources[strid]->handle_payload(payload, size);
  } else {
    // Really bad -> unexpeced StreamID in UDP Payload.
    // This check is needed in order to avoid dynamically add thousands
    // of Sources on the fly, in case the data corruption is extremely severe.
    if (m_num_unexid_frames.count(strid) == 0) {
      m_num_unexid_frames[strid] = 0;
    }
    m_num_unexid_frames[strid]++;
  }
}

void 
NICReceiver::set_running(bool should_run)
{
  bool was_running = m_run_marker.exchange(should_run);
  TLOG_DEBUG(5) << "Active state was toggled from " << was_running << " to " << should_run;
}

} // namespace dpdklibs
} // namespace dunedaq

// #include "detail/NICReceiver.hxx"

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICReceiver)
