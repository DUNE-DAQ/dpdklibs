/**
 * @file NICReader.cc NICReader DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

#include "NICReader.hpp"

#include "logging/Logging.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * @brief Name used by TRACE TLOG calls from this source file
 */
#define TRACE_NAME "NICReader" // NOLINT

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

NICReader::NICReader(const std::string& name)
  : DAQModule(name)
{
  register_command("conf", &NICReader::do_configure);
  register_command("start", &NICReader::do_start);
  register_command("stop", &NICReader::do_stop);
}

void
NICReader::init(const data_t& args)
{

}

void
NICReader::do_configure(const data_t& args)
{

}

void
NICReader::do_start(const data_t& args)
{

}

void
NICReader::do_stop(const data_t& args)
{

}

void
NICReader::get_info(opmonlib::InfoCollector& ci, int level)
{

}

} // namespace dpdklibs
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICReader)
