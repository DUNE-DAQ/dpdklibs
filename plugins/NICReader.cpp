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


static int
lcore_hello(__attribute__((unused)) void *arg)
{
  unsigned lcore_id;
  lcore_id = rte_lcore_id();
  printf("hello from core %u\n", lcore_id);
  return 0;
}

void
NICReader::do_configure(const data_t& args)
{
  int ret;
  unsigned lcore_id;

  //char[] eal_args = "-l 0-4 -n 3";
  char* eal_args[] = {"-n", "4", "-l", "0-3"};

  ret = rte_eal_init(4, eal_args);
  if (ret < 0)
    rte_panic("Cannot init EAL\n");

  /* call lcore_hello() on every slave lcore */
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
  }

  /* call it on master lcore too */
  lcore_hello(NULL);

  rte_eal_mp_wait_lcore();
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
