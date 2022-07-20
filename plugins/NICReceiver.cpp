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
#include "dpdklibs/flow_control.h"

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
    m_run_marker{ false }
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
  ealutils::finish_eal();
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
  m_num_frames[0] = { 0 };
  m_num_frames[1] = { 0 };
  m_num_frames[2] = { 0 };
  m_num_frames[3] = { 0 };

  // TLOG_DEBUG(TLVL_ENTER_EXIT_METHODS)
  TLOG() << get_name() << ": Entering do_conf() method";
  m_cfg = args.get<module_conf_t>();

  // Source mapping setup. For now, baked in...
  TLOG() << "Setting up AMC[0] data queue.";
  m_amc_data_queues[0] = std::make_unique<amc_frame_queue_t>(m_amc_queue_capacity);
  TLOG() << "Setting up AMC[0] parser.";
  m_amc_frame_handlers[0] = std::make_unique<readoutlibs::ReusableThread>(0);
  m_amc_frame_handlers[0]->set_name(m_parser_thread_name, 0);

  TLOG() << "Setting up AMC[1] data queue.";
  m_amc_data_queues[1] = std::make_unique<amc_frame_queue_t>(m_amc_queue_capacity);
  TLOG() << "Setting up AMC[1] parser.";
  m_amc_frame_handlers[1] = std::make_unique<readoutlibs::ReusableThread>(0);
  m_amc_frame_handlers[1]->set_name(m_parser_thread_name, 1);

  TLOG() << "Setting up AMC[2] data queue.";
  m_amc_data_queues[2] = std::make_unique<amc_frame_queue_t>(m_amc_queue_capacity);
  TLOG() << "Setting up AMC[2] parser.";
  m_amc_frame_handlers[2] = std::make_unique<readoutlibs::ReusableThread>(0);
  m_amc_frame_handlers[2]->set_name(m_parser_thread_name, 2);

  TLOG() << "Setting up AMC[3] data queue.";
  m_amc_data_queues[3] = std::make_unique<amc_frame_queue_t>(m_amc_queue_capacity);
  TLOG() << "Setting up AMC[3] parser.";
  m_amc_frame_handlers[3] = std::make_unique<readoutlibs::ReusableThread>(0);
  m_amc_frame_handlers[3]->set_name(m_parser_thread_name, 3);

  // DPDK setup
  std::vector<char*> eal_params = ealutils::string_to_eal_args(m_cfg.eal_arg_list);
  TLOG() << "Setting up EAL with params from config.";
  // int argc = 4;
  // std::vector<char*> v{"-l", "0-1", "-n", "3"};
  ealutils::init_eal(eal_params.size(), eal_params.data());
  // ealutils::init_eal(argc, v.data());

  int nb_ports = ealutils::get_available_ports();
  TLOG() << "Allocating pools and mbufs";

// RS -> Fixme never
/////////////////////////////////////////////////////////////////////////////////
  const int nb_queues = 4;
////////////////////////////////////////////////////////////////////////////////

  for (int i=0; i<nb_queues; ++i) {
    std::stringstream ss;
    ss << "MBUFPOOL-" << i; 
    m_mbuf_pools[i] = ealutils::get_mempool(ss.str());
    m_bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
    rte_pktmbuf_alloc_bulk(m_mbuf_pools[i].get(), m_bufs[i], m_burst_size);
  }
  TLOG() << "Setting up only port 0.";
  ealutils::port_init(0, nb_queues, 0, m_mbuf_pools); // just init port 0.



///////////////////////////////////////////////
    struct rte_flow_error error;
    struct rte_flow *flow;

    const IpAddr src_ip("24.16.8.0");
    const IpAddr dst_ip("192.168.8.1");
    const IpAddr full_mask("255.255.255.255");
    const IpAddr empty_mask("0.0.0.0");

    const IpAddr src_ip16("10.73.139.16");
    const IpAddr dst_ip16("192.168.8.1");
    const IpAddr full_mask16("255.255.255.255");
    const IpAddr empty_mask16("0.0.0.0");

    /* closing and releasing resources */
    //rte_flow_flush(0, &error);


    flow = generate_ipv4_flow(0, 0,
    			src_ip, empty_mask,
    			dst_ip, full_mask, &error);

    flow = generate_ipv4_flow(0, 1,
    			src_ip, empty_mask,
    			dst_ip, full_mask, &error);

    flow = generate_ipv4_flow(0, 2,
    			src_ip, empty_mask,
    			dst_ip, full_mask, &error);

    flow = generate_ipv4_flow(0, 3,
    			src_ip, empty_mask,
    			dst_ip, full_mask, &error);


//    /* >8 End of create flow and the flow rule. */
//    if (not flow) {
//    	printf("Flow can't be created %d message: %s\n",
//    		error.type,
//    		error.message ? error.message : "(no stated reason)");
//    	rte_exit(EXIT_FAILURE, "error in creating flow");
//    }
//    /* >8 End of creating flow for send packet with. */


    // Adding second flow
    flow = generate_drop_flow(0, &error);
    /* >8 End of create flow and the flow rule. */
    if (not flow) {
    	printf("Flow can't be created %d message: %s\n",
    		error.type,
    		error.message ? error.message : "(no stated reason)");
    	rte_exit(EXIT_FAILURE, "error in creating flow");
    }
////////////////////////////////////////////


  
  //m_mbuf_pool = ealutils::setup_eal(eal_params.size(), eal_params.data());//&eal_params[0]);
  //TLOG() << "Allocating RTE_MBUFs.";
  //m_bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
  //rte_pktmbuf_alloc_bulk(m_mbuf_pool.get(), m_bufs, m_burst_size);
  TLOG() << "RTE configured.";
}

void
NICReceiver::do_start(const data_t& args)
{
  if (!m_run_marker.load()) {
    set_running(true);
    m_dpdk_quit_signal = 0;
    ealutils::dpdk_quit_signal = 0;

    m_stat_thread = std::thread([&]() {
      while (m_run_marker.load()) {
        // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
        TLOG() << "Received Rate of q0 is " << size_t(9000) * m_num_frames[0] / 1e6 * 8;
        TLOG() << "Received Rate of q1 is " << size_t(9000) * m_num_frames[1] / 1e6 * 8;
        TLOG() << "Received Rate of q2 is " << size_t(9000) * m_num_frames[2] / 1e6 * 8;
        TLOG() << "Received Rate of q3 is " << size_t(9000) * m_num_frames[3] / 1e6 * 8;
	TLOG() << "CLEARED  Rate is " << size_t(9000) * m_cleaned / 1e6 * 8;
        // printf("Rate is %f\n", (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8);
        m_num_frames[0].exchange(0);
        m_num_frames[1].exchange(0);
        m_num_frames[2].exchange(0);
        m_num_frames[3].exchange(0);
	m_cleaned.exchange(0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });
    TLOG() << "Stats thread started.";

    TLOG() << "Starting frame processors.";
    for (unsigned int i=0; i<m_amc_frame_handlers.size(); ++i) {
      m_amc_frame_handlers[i]->set_work(&NICReceiver::handle_frame_queue, this, i);
    }

    // Call lcore_main on the main core only
    // int res = rte_eal_remote_launch((lcore_function_t *) lcore_main, mbuf_pool, 1);
    // int res2 = rte_eal_remote_launch((lcore_function_t *) lcore_main, mbuf_pool, 2);
    //rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, NULL, SKIP_MAIN);
    int ret1 = rte_eal_remote_launch((int (*)(void*))(&NICReceiver::rx_runner), this, 1); //SKIP_MAIN); // cast (int (*)(void*))
    int ret2 = rte_eal_remote_launch((int (*)(void*))(&NICReceiver::rx_runner), this, 2);
    int ret3 = rte_eal_remote_launch((int (*)(void*))(&NICReceiver::rx_runner), this, 3);
    int ret4 = rte_eal_remote_launch((int (*)(void*))(&NICReceiver::rx_runner), this, 4);
    //ret = rte_eal_mp_remote_launch(launch_one_lcore, NULL, SKIP_MAIN);

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
  TLOG() << "STOP CALLED";
  if (m_run_marker.load()) {
    TLOG() << "Raising quit through variables!";
    set_running(false);
    m_dpdk_quit_signal = 1;
    ealutils::dpdk_quit_signal = 1;
    TLOG() << "Signal raised!";
    int ret = ealutils::wait_for_lcores();
    TLOG() << "Lcores finished!";

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
NICReceiver::handle_frame_queue(int id)
{
  TLOG() << "frame_proc[" << id << "] starting to handle frames in corresponding queue!";

  while (m_run_marker.load()) {
    detdataformats::tde::TDE16Frame frame;
    if (m_amc_data_queues[id]->read(frame)) {
      m_cleaned++;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

inline void
dump_to_buffer(const char* data,
               std::size_t size,
               void* buffer,
               uint32_t buffer_pos, // NOLINT
               const std::size_t& buffer_size)
{
  auto bytes_to_copy = size; // NOLINT
  while (bytes_to_copy > 0) {
    auto n = std::min(bytes_to_copy, buffer_size - buffer_pos); // NOLINT
    std::memcpy(static_cast<char*>(buffer) + buffer_pos, data, n);
    buffer_pos += n;
    bytes_to_copy -= n;
    if (buffer_pos == buffer_size) {
      buffer_pos = 0;
    }
  }
}

void
NICReceiver::copy_out(int queue, char* message, std::size_t size) {
  detdataformats::tde::TDE16Frame target_payload;
  uint32_t bytes_copied = 0;
  dump_to_buffer(message, size, static_cast<void*>(&target_payload), bytes_copied, sizeof(target_payload));
  m_amc_data_queues[queue]->write(std::move(target_payload));
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
