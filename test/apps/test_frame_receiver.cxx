
/* Application will run until quit or killed. */
#include <fmt/core.h>
#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <tuple>

#include <csignal>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include "detdataformats/DAQEthHeader.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "logging/Logging.hpp"
#include "dpdklibs/EALSetup.hpp"
#include "opmonlib/InfoCollector.hpp"
#include "opmonlib/OpmonService.hpp"
#include "opmonlib/JSONTags.hpp"

#include "dpdklibs/receiverinfo/InfoNljs.hpp"

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define NSTREAM 128

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;


namespace {

  constexpr int64_t expected_timestamp_step = 2048;
  constexpr int64_t expected_seq_id_step = 1;
  int64_t expected_packet_size = PacketInfoAccumulator::s_ignorable_value;
  
  // Might need to make the expected packet size settable after construction
  PacketInfoAccumulator processor(expected_seq_id_step,
				  expected_timestamp_step);
  
    std::ostream & operator <<(std::ostream &out, const StreamUID &obj) {
      return out << static_cast<std::string>(obj);
    }

    // Apparently only 8 and above works for "burst_size"

    // From the dpdk documentation, describing the rte_eth_rx_burst
    // function (and keeping in mind that their "nb_pkts" variable is the
    // same as our "burst size" variable below):
    // "Some drivers using vector instructions require that nb_pkts is
    // divisible by 4 or 8, depending on the driver implementation."

    bool check_timestamp = true;
    bool per_stream_reports         = true;

    constexpr int burst_size = 256;

    constexpr int max_packets_to_dump = 10; 
    int dumped_packet_count           = 0;

    std::atomic<int64_t> num_packets            = 0;
    std::atomic<int64_t> num_bytes              = 0;
    std::atomic<int64_t> total_packets          = 0;
    std::atomic<int64_t> non_ipv4_packets       = 0;

    std::ofstream datafile;
    const std::string output_data_filename = "dpdklibs_test_frame_receiver.dat";


} // namespace


static const struct rte_eth_conf iface_conf_default = { 
    .rxmode = {
        .mtu = 9000,
        .offloads = (DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM),
    } 
};

std::vector<char*> construct_argv(std::vector<std::string> &std_argv){
    std::vector<char*> vec_argv;
    for (int i=0; i < std_argv.size() ; i++){
        vec_argv.insert(vec_argv.end(), std_argv[i].data());
    }
    return vec_argv;
}

static int lcore_main(struct rte_mempool* mbuf_pool, uint16_t iface, uint64_t time_per_report){
    /*
     * Check that the iface is on the same NUMA node as the polling thread
     * for best performance.
     */

    if (rte_eth_dev_socket_id(iface) >= 0 && rte_eth_dev_socket_id(iface) != static_cast<int>(rte_socket_id())) {
        fmt::print(
            "WARNING, iface {} is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", iface
        );
    }

    auto stats = std::thread([&]() {

         auto service = opmonlib::makeOpmonService("stdout");			       
	 int64_t udp_pkt_counter = 0;
	 int64_t total_bad_seq_id = 0;
	 int64_t total_bad_timestamp = 0;
	 int64_t total_bad_payload_size = 0;
	 int64_t max_timestamp_skip = 0;
	 int64_t max_seq_id_skip = 0;
	 int64_t min_payload_size = std::numeric_limits<int64_t>::max();
	 int64_t max_payload_size = std::numeric_limits<int64_t>::min();
	 
	 
         while (true) {

	    opmonlib::InfoCollector ic;

	    uint64_t packets_per_second = num_packets / time_per_report;
            uint64_t bytes_per_second   = num_bytes   / time_per_report;

	    auto receiver_stats_by_stream = processor.get_and_reset_stream_stats();
	    
	    int64_t num_bad_seq_id = 0;
	    int64_t num_bad_timestamp = 0;
	    int64_t num_bad_payload_size = 0;
	    
            num_packets.exchange(0);
            num_bytes.exchange(0);
	    
            for ( auto& [suid, stats] : receiver_stats_by_stream) {

		receiverinfo::Info derived_stats = DeriveFromReceiverStats( receiver_stats_by_stream[suid], time_per_report);

		fmt::print("\nStream {:15}: n.pkts {} (tot. {})", (std::string)suid, receiver_stats_by_stream[suid].packets_since_last_reset, derived_stats.total_packets);
                if (per_stream_reports){

		  std::stringstream seqidstr;
		  if (expected_seq_id_step != PacketInfoAccumulator::s_ignorable_value) {
		    seqidstr << derived_stats.bad_seq_id_packets_per_second;
		  } else {
		    seqidstr << "(ignored)";
		  }

		  std::stringstream timestampstr;
		  if (expected_timestamp_step != PacketInfoAccumulator::s_ignorable_value) {
		    timestampstr << derived_stats.bad_ts_packets_per_second;
		  } else {
		    timestampstr << "(ignored)";
		  }

		  std::stringstream sizestr;
		  if (expected_packet_size != PacketInfoAccumulator::s_ignorable_value) {
		    sizestr << derived_stats.bad_size_packets_per_second;
		  } else {
		    sizestr << "(ignored)";
		  }
		  
                    fmt::print(
                        " {:8.3f} MiB/s, seq. jumps/s: {}, ts jumps/s: {}, unexpected size/s: {}", 
                        derived_stats.bytes_per_second / (1024.*1024.), seqidstr.str(), timestampstr.str(), sizestr.str()
			       );
                }

		udp_pkt_counter += receiver_stats_by_stream[suid].packets_since_last_reset;

		num_bad_seq_id += receiver_stats_by_stream[suid].bad_seq_ids_since_last_reset;
		num_bad_timestamp += receiver_stats_by_stream[suid].bad_timestamps_since_last_reset;
		num_bad_payload_size += receiver_stats_by_stream[suid].bad_sizes_since_last_reset;
		
		max_seq_id_skip = derived_stats.max_bad_seq_id_deviation > max_seq_id_skip ? derived_stats.max_bad_seq_id_deviation : max_seq_id_skip;
		max_timestamp_skip = derived_stats.max_bad_ts_deviation > max_timestamp_skip ? derived_stats.max_bad_ts_deviation : max_timestamp_skip;
		max_payload_size = receiver_stats_by_stream[suid].max_packet_size > max_payload_size ?
		  receiver_stats_by_stream[suid].max_packet_size : max_payload_size;
		min_payload_size = receiver_stats_by_stream[suid].min_packet_size < min_payload_size ?
										    receiver_stats_by_stream[suid].min_packet_size : min_payload_size;
		
		opmonlib::InfoCollector tmp_ic;

		tmp_ic.add(derived_stats);

		ic.add(udp::get_opmon_string(suid), tmp_ic);
            }

	    total_bad_seq_id += num_bad_seq_id;
	    total_bad_timestamp += num_bad_timestamp;
	    total_bad_payload_size += num_bad_payload_size;
	    
            fmt::print(
                "\nSince the last report {} seconds ago:\n"
                "Packets/s: {} Bytes/s: {} Total packets: {} Non-IPV4 packets: {} Total UDP packets: {}\n"
                "Packets with wrong sequence id: {}, Max wrong seq_id jump {}, Total Packets with Wrong seq_id {}\n"
                "Max udp payload: {}, Min udp payload: {}\n",
                time_per_report,
                packets_per_second, bytes_per_second, total_packets, non_ipv4_packets, udp_pkt_counter,
                num_bad_seq_id, max_seq_id_skip, total_bad_seq_id,
                max_payload_size, min_payload_size
            );

            if (expected_packet_size != PacketInfoAccumulator::s_ignorable_value){
                fmt::print(
                    "Packets with wrong payload size: {}, Total Packets with Wrong size {}\n",
                    num_bad_payload_size, total_bad_payload_size
                );
            }

            if (expected_timestamp_step != PacketInfoAccumulator::s_ignorable_value) {
                fmt::print(
                    "Wrong Timestamp difference Packets: {}, Max wrong Timestamp difference overall {}, Total Packets with Wrong Timestamp {}\n",
                    num_bad_timestamp, max_timestamp_skip, total_bad_timestamp
                );
            }

            fmt::print("\n");


		// Following logic taken from InfoManager::gather_info in opmonlib
		nlohmann::json j_info;
		nlohmann::json j_parent;
	    
		j_info = ic.get_collected_infos();
		j_parent[opmonlib::JSONTags::parent] = {};
		j_parent[opmonlib::JSONTags::parent].swap(j_info[opmonlib::JSONTags::children]);
		j_parent[opmonlib::JSONTags::tags] = { { "partition_id", "test_frame_receiver_partition" } } ;
		//service->publish(j_parent);

            std::this_thread::sleep_for(std::chrono::seconds(time_per_report));
        }
    });

    struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
    rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

    datafile.open(output_data_filename, std::ios::out | std::ios::binary);
    if ( (datafile.rdstate() & std::ofstream::failbit ) != 0 ) {
        fmt::print("WARNING: Unable to open output file \"{}\"\n", output_data_filename);
    }

    while (true) {
        /* Get burst of RX packets, from first iface of pair. */
        const uint16_t nb_rx = rte_eth_rx_burst(iface, 0, bufs, burst_size);
	
        num_packets   += nb_rx;
        total_packets += nb_rx;

        for (int i_b = 0; i_b < nb_rx; ++i_b) {
            num_bytes += bufs[i_b]->pkt_len;
            

            bool dump_packet = false;
            if (not RTE_ETH_IS_IPV4_HDR(bufs[i_b]->packet_type)) {
                non_ipv4_packets++;
                dump_packet = true;
                continue;
            }

	    bool bad_seq_id_found = false;
	    bool bad_timestamp_found = false;
	    bool bad_payload_size_found = false;
	    
	    processor.process_packet(bufs[i_b], bad_seq_id_found, bad_timestamp_found, bad_payload_size_found);

            if (bad_seq_id_found || bad_timestamp_found || bad_payload_size_found) {
	      dump_packet = true;
            }

            if (dump_packet && dumped_packet_count < max_packets_to_dump) {
                dumped_packet_count++;

                rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);
            }
        }

        rte_pktmbuf_free_bulk(bufs, nb_rx);
    }

    return 0;
}                                                              

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum){
    fmt::print("Caught signal {}\n", signum);
    if (datafile.is_open()) {
        datafile.close();
    }
    // Terminate program
    std::exit(signum);
}

int main(int argc, char** argv){
    uint64_t time_per_report = 1;
    uint16_t iface = 0;

    CLI::App app{"test frame receiver"};
    app.add_option("-s", expected_packet_size, "Expected frame size");
    app.add_option("-i", iface, "Interface to init");
    app.add_option("-t", time_per_report, "Time Per Report");
    app.add_flag("--check-time", check_timestamp, "Report back differences in timestamp");
    app.add_flag("-p", per_stream_reports, "Detailed per stream reports");
    CLI11_PARSE(app, argc, argv);

    processor.set_expected_packet_size(expected_packet_size);
    
    //    define function to be called when ctrl+c is called.
    std::signal(SIGINT, signal_callback_handler);
    
    std::vector<std::string> eal_args;
    eal_args.push_back("dpdklibds_test_frame_receiver");


    // initialise eal with constructed argc and argv
    std::vector<char*> vec_argv = construct_argv(eal_args);
    char** constructed_argv = vec_argv.data();
    int constructed_argc = eal_args.size();

    int ret = rte_eal_init(constructed_argc, constructed_argv);
    if (ret < 0) {rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");}

    auto n_ifaces = rte_eth_dev_count_avail();
    fmt::print("# of available ifaces: {}\n", n_ifaces);
    if (n_ifaces == 0){
        std::cout << "WARNING: no available ifaces. exiting...\n";
        rte_eal_cleanup();
        return 1;
    }

    // Allocate pools and mbufs per queue
    std::map<int, std::unique_ptr<rte_mempool>> mbuf_pools;
    std::map<int, struct rte_mbuf **> bufs;
    uint16_t n_rx_qs = 1;

    std::cout << "Allocating pools and mbufs.\n";
    for (size_t i=0; i<n_rx_qs; ++i) {
        std::stringstream ss;
        ss << "MBP-" << i;
        fmt::print("Pool acquire: {}\n", ss.str()); 
        mbuf_pools[i] = ealutils::get_mempool(ss.str());
        bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
        rte_pktmbuf_alloc_bulk(mbuf_pools[i].get(), bufs[i], burst_size);
    }

    // Setting up only one iface
    fmt::print("Initialize only iface {}!\n", iface);
    ealutils::iface_init(iface, n_rx_qs, 0, mbuf_pools); // just init iface, no TX queues

    lcore_main(mbuf_pools[0].get(), iface, time_per_report);

    rte_eal_cleanup();

    return 0;
}
