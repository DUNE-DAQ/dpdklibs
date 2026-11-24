/**
 * @file DPDKFakeDataSender.cpp
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "DPDKFakeDataSender.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appmodel/DPDKFakeDataSender.hpp"
#include "appmodel/DPDKFakeDataSenderConf.hpp"
#include "appmodel/NWDetDataReceiver.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/NetworkDetectorToDaqConnection.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/NetworkInterface.hpp"
#include "datahandlinglibs/DataMoveCallbackRegistry.hpp"

#include <memory>
#include <string>

#define TRACE_NAME "DPDKFakeDataSender" // NOLINT

namespace dunedaq::dpdklibs {

DPDKFakeDataSender::DPDKFakeDataSender(const std::string& name)
  : DAQModule(name)
  , m_core(name)
{
  register_command("conf", &DPDKFakeDataSender::do_configure);
  register_command("start", &DPDKFakeDataSender::do_start);
  register_command("enable_triggers", &DPDKFakeDataSender::do_enable_triggers);
  register_command("stop_trigger_sources", &DPDKFakeDataSender::do_stop);
  register_command("scrap", &DPDKFakeDataSender::do_scrap);
}

DPDKFakeDataSender::~DPDKFakeDataSender()
{
  m_core.close_dpdk();
}

void
DPDKFakeDataSender::init(const std::shared_ptr<appfwk::ConfigurationManager> cfg)
{
  m_cfg = cfg;
  m_core.init<appmodel::DPDKFakeDataSender>(*m_cfg);
}

void
DPDKFakeDataSender::do_configure(const CommandData_t& /*args*/)
{
  auto registry = datahandlinglibs::DataMoveCallbackRegistry::get();
  m_core.configure(*registry);
}

void
DPDKFakeDataSender::do_start(const CommandData_t& /*args*/)
{
  m_core.start();
}

void
DPDKFakeDataSender::do_enable_triggers(const CommandData_t& /*args*/)
{
  m_core.enable_triggers();
}

void
DPDKFakeDataSender::do_stop(const CommandData_t& /*args*/)
{
  m_core.stop();
}

void
DPDKFakeDataSender::do_scrap(const CommandData_t& /*args*/)
{
  m_core.scrap();
}

} // namespace dunedaq::dpdklibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::DPDKFakeDataSender)
