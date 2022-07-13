/**
 * @file NICSender.cpp NICSender DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

#include "logging/Logging.hpp"
#include "dpdklibs/EALSetup.hpp"

#include "NICSender.hpp"

#include <cinttypes>
#include <chrono>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * @brief Name used by TRACE TLOG calls from this source file
 */
#define TRACE_NAME "NICSender" // NOLINT

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

NICSender::NICSender(const std::string& name)
  : DAQModule(name)
{
  register_command("conf", &NICSender::do_configure);
  register_command("start", &NICSender::do_start);
  register_command("stop", &NICSender::do_stop);
  register_command("scrap", &NICSender::do_scrap);
}

NICSender::~NICSender()
{
}

void
NICSender::init(const data_t& args)
{
}

void
NICSender::do_configure(const data_t& args)
{
}

void
NICSender::do_start(const data_t& args)
{

}

void
NICSender::do_stop(const data_t& args)
{

}

void
NICSender::do_scrap(const data_t& args)
{
}

void
NICSender::get_info(opmonlib::InfoCollector& ci, int level)
{

}

} // namespace dpdklibs
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICSender)
