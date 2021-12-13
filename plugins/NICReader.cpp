/**
 * @file NICReader.cc NICReader DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

#include "NICReader.hpp"
#include "dpdklibs/AvailableCallbacks.hpp"

#include "logging/Logging.hpp"

#include <stdint.h>
#include <inttypes.h>
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
  register_command("scrap", &NICReader::do_scrap);
}

NICReader::~NICReader()
{
  /* clean up the EAL */
  rte_eal_cleanup();
}

void
NICReader::init(const data_t& args)
{

}

/// conf for hello world callback
//void
//NICReader::do_configure(const data_t& args)
//{
//  auto config = args.get<module_conf_t>();
//  auto arg_count = config.arg_list.size();
//  // std::vector<std::string> to char** conversion
//  char* eal_args[arg_count];
//  for (int i = 0; i < arg_count; ++i) {
//    eal_args[i] = const_cast<char*>(config.arg_list[i].c_str());
//  }
//
//  auto ret = rte_eal_init(arg_count, eal_args);
//  if (ret < 0)
//    rte_panic("Cannot init EAL\n");
//
//  /* call lcore_hello() on every slave lcore */
//  unsigned lcore_id;
//  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
//    rte_eal_remote_launch(callbacks::lcore_hello, NULL, lcore_id);
//  }
//
//  /* call it on master lcore too */
//  callbacks::lcore_hello(NULL);
//
//  rte_eal_mp_wait_lcore();
//}

/// conf for basic skeleton forwarding callback
void
NICReader::do_configure(const data_t& args)
{
  auto config = args.get<module_conf_t>();
  auto arg_count = config.arg_list.size();
  // std::vector<std::string> to char** conversion
  char* eal_args[arg_count];
  for (int i = 0; i < arg_count; ++i) {
    eal_args[i] = const_cast<char*>(config.arg_list[i].c_str());
  }

  struct rte_mempool* mbuf_pool;
  unsigned nb_ports;
  uint16_t portid;

  /* Initialize the Environment Abstraction Layer (EAL). */
  int ret = rte_eal_init(arg_count, eal_args);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

//  arg_count -= ret;
//  eal_args += ret;

  /* Check that there is an even number of ports to send/receive on. */
  nb_ports = rte_eth_dev_count_avail();
  printf("nb_ports: %i\n", &nb_ports);
  if (nb_ports < 2 || (nb_ports & 1))
    rte_exit(EXIT_FAILURE, "Error: number of ports must be even\n");

  /* Creates a new mempool in memory to hold the mbufs. */
  mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
                                      MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

  unsigned lcore_id;
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    printf("lcore_id: %i\n", lcore_id);
  }

  /* Initialize all ports. */
  RTE_ETH_FOREACH_DEV(portid)
  if (callbacks::port_init(portid, mbuf_pool) != 0)
    rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
             portid);

  if (rte_lcore_count() > 1) {
    printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
    m_running = 1;

    rte_eal_remote_launch(callbacks::lcore_main, &m_running, 2);
  }

//  /* Call lcore_main on the main core only. */
//  callbacks::lcore_main(&m_running);
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
NICReader::do_scrap(const data_t& args)
{
  m_running = 0;
  rte_eal_mp_wait_lcore();
}

void
NICReader::get_info(opmonlib::InfoCollector& ci, int level)
{

}

} // namespace dpdklibs
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICReader)
