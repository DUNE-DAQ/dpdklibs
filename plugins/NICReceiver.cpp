/**
 * @file NICReceiver.cpp NICReceiver DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

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
  register_command("stop_trigger_sources", &NICReceiver::do_stop);
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
NICReceiver::init(const data_t& args)
{
 auto ini = args.get<appfwk::app::ModInit>();
 for (const auto& qi : ini.conn_refs) {
    if (qi.uid == "errored_chunks_q") {
      continue;
    } else {
      TLOG_DEBUG(TLVL_WORK_STEPS) << ": NICCardReader output queue is " << qi.uid;
      const char delim = '_';
      std::string target = qi.uid;
      std::vector<std::string> words;
      tokenize(target, delim, words);
      int sourceid = -1;
      try {
        sourceid = std::stoi(words.back());
      } catch (const std::exception& ex) {
        ers::fatal(dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "Output link ID could not be parsed on queue instance name! "));
      }
      bool callback_mode = false;
      if (words.front() == "cb") {
        callback_mode = true;
      }
      TLOG() << "Creating source for target queue: " << target << " DLH number: " << sourceid << " CallbacksMode?=" << callback_mode;
      m_sources[sourceid] = createSourceModel(qi.uid, callback_mode);
      if (m_sources[sourceid] == nullptr) {
        ers::fatal(dunedaq::readoutlibs::InitializationError(
          ERS_HERE, "CreateSource failed to provide an appropriate model for queue!"));
      }
      m_sources[sourceid]->init(args);
    }
  }
}

void
NICReceiver::do_configure(const data_t& args)
{
  TLOG() << get_name() << ": Entering do_conf() method";
  m_cfg = args.get<module_conf_t>();
  auto ifaces_cfg = m_cfg.ifaces;

  // EAL setup
  TLOG() << "Setting up EAL with params from config.";
  std::vector<std::string> eal_args;

  eal_args.push_back("eal_cmdline");

  // Enforce the process type to primary  
  eal_args.push_back("--proc-type=primary");

  // Construct the pcie devices allowed mask
  for (const auto& iface_cfg : ifaces_cfg) {
     eal_args.push_back("-a");
     eal_args.push_back(iface_cfg.pci_addr);
  }
  
  // Use the first pcie device id as file prefix
  // FIXME: Review this strategy - should work in most of cases, but it could be 
  // confusing in configs with multiple interfaces
  eal_args.push_back(fmt::format("--file-prefix={}", ifaces_cfg.front().pci_addr));

  // Append the remaining dpdk parameters
  std::istringstream iss(m_cfg.eal_arg_list);
  std::string arg_from_str;
  while (iss >> arg_from_str) {
    if (!arg_from_str.empty()) {
      eal_args.push_back(arg_from_str);
    }
  }

  ealutils::init_eal(eal_args);

  // Get available Interfaces from EAL
  auto available_ifaces = ifaceutils::get_num_available_ifaces();
  TLOG() << "Number of available interfaces: " << available_ifaces;
  for (unsigned int ifc_id=0; ifc_id<available_ifaces; ++ifc_id) {
    std::string mac_addr_str = ifaceutils::get_iface_mac_str(ifc_id);
    std::string pci_addr_str = ifaceutils::get_iface_pci_str(ifc_id);
    m_mac_to_id_map[mac_addr_str] = ifc_id;
    m_pci_to_id_map[pci_addr_str] = ifc_id;
    TLOG() << "Available iface with MAC=" << mac_addr_str << " PCIe=" <<  pci_addr_str << " logical ID=" << ifc_id;
  }

  // Configure expected (and available!) interfaces
  for (const auto& iface_cfg : ifaces_cfg) {
     auto iface_mac_addr = iface_cfg.mac_addr;
     auto iface_pci_addr = iface_cfg.pci_addr;
     if ((m_mac_to_id_map.count(iface_mac_addr) != 0) && 
         (m_pci_to_id_map.count(iface_pci_addr) != 0)) {
       auto iface_id = m_mac_to_id_map[iface_mac_addr];
       TLOG() << "Configuring expected interface with MAC=" << iface_mac_addr 
              << " PCIe=" << iface_pci_addr << " Logical ID=" << iface_id;
       m_ifaces[iface_id] = std::make_unique<IfaceWrapper>(iface_id, m_sources, m_run_marker);
       m_ifaces[iface_id]->conf(iface_cfg);
       m_ifaces[iface_id]->allocate_mbufs();
       m_ifaces[iface_id]->setup_interface();
       m_ifaces[iface_id]->setup_flow_steering();
       m_ifaces[iface_id]->setup_xstats();
     } else {
       TLOG() << "No available interface with MAC=" << iface_mac_addr << " PCI=" << iface_pci_addr;
       throw dunedaq::readoutlibs::InitializationError(ERS_HERE, "NICReceiver configuration failed due expected but unavailable interface!");
     }
  }
  
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

  return;
}

void
NICReceiver::do_start(const data_t&)
{
  // TLOG() << get_name() << ": Entering do_start() method";
  // if (!m_run_marker.load()) {
  //   set_running(true);
  //   TLOG() << "Starting iface wrappers.";
  //   for (auto& [iface_id, iface] : m_ifaces) {
  //     iface->start();
  //   }
  // } else {
  //   TLOG_DEBUG(5) << "NICReader is already running!";
  // }
  //
  
  // Setup callbacks on all sourcemodels
  for (auto& [sourceid, source] : m_sources) {
    source->acquire_callback();
  }

  for (auto& [iface_id, iface] : m_ifaces) {
    iface->enable_flow();
  }
}

void
NICReceiver::do_stop(const data_t&)
{
  // TLOG() << get_name() << ": Entering do_stop() method";
  // if (m_run_marker.load()) {
  //   TLOG() << "Raising stop through variables!";
  //   set_running(false);
  //   TLOG() << "Stopping iface wrappers.";
  //   for (auto& [iface_id, iface] : m_ifaces) {
  //     iface->stop();
  //   }
  //   ealutils::wait_for_lcores();
  //   TLOG() << "Stoppped DPDK lcore processors and internal threads...";
  // } else {
  //   TLOG_DEBUG(5) << "DPDK lcore processor is already stopped!";
  // }
  // return;
  for (auto& [iface_id, iface] : m_ifaces) {
    iface->disable_flow();
  }
}

void
NICReceiver::do_scrap(const data_t&)
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

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICReceiver)
