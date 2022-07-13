/**
 * @file NICReceiver.cpp NICReceiver DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/nicreader/Nljs.hpp"

#include "logging/Logging.hpp"
#include "detdataformats/tde/TDE16Frame.hpp"

#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"

#include "NICReceiver.hpp"

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
    m_mbuf_pool(nullptr)
{
  register_command("conf", &NICReceiver::do_configure);
  register_command("start", &NICReceiver::do_start);
  register_command("stop", &NICReceiver::do_stop);
  register_command("scrap", &NICReceiver::do_scrap);
}

NICReceiver::~NICReceiver()
{
  //TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) 
  TLOG() << get_name() << ": Destructor called. Tearing down EAL.";
  finish_eal();
}

void
NICReceiver::init(const data_t& args)
{
  //TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) 
  TLOG() << get_name() << ": Entering init() method";
}

void
NICReceiver::do_configure(const data_t& args)
{
  //TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS) 
  TLOG() << get_name() << ": Entering do_conf() method";
  m_cfg = args.get<module_conf_t>();
  std::vector<char*> eal_params = string_to_eal_args(m_cfg.eal_arg_list);
  TLOG() << "Setting up EAL with params from config.";
  m_mbuf_pool = setup_eal(eal_params.size(), eal_params.data());//&eal_params[0]);
  TLOG() << "Allocating RTE_MBUFs.";
  m_bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
  rte_pktmbuf_alloc_bulk(m_mbuf_pool.get(), m_bufs, m_burst_size);
}

void
NICReceiver::do_start(const data_t& args)
{
  if (!m_run_marker.load()) {
    set_running(true);

    m_stat_thread = std::thread([&]() {
      while (m_run_marker.load()) {
        // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
        TLOG() << "Rate is " << size_t(9000) * m_num_frames / 1e6 * 8;
        // printf("Rate is %f\n", (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8);
        m_num_frames.exchange(0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });

    TLOG() << "Stats thread started.";

    rx_runner<detdataformats::tde::TDE16Frame>();
    TLOG() << "RX runner started.";

    // launch core proc
    TLOG_DEBUG(5) << "Started DPDK lcore processor...";
  } else {
    TLOG_DEBUG(5) << "DPDK lcore processor is already running!";
  }
}

void
NICReceiver::do_stop(const data_t& args)
{
  if (m_run_marker.load()) {
    set_running(false);
    // should wait for thread to join
    TLOG_DEBUG(5) << "Stoppped DPDK lcore processor...";
  } else {
    TLOG_DEBUG(5) << "DPDK lcore processor is already stopped!";
  }
}

void
NICReceiver::do_scrap(const data_t& args)
{
}

void
NICReceiver::get_info(opmonlib::InfoCollector& ci, int level)
{

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
