
/* Application will run until quit or killed. */
#include <fmt/core.h>
#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <stdint.h>

#include <csignal>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "detdataformats/DAQEthHeader.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "logging/Logging.hpp"
#include "dpdklibs/EALSetup.hpp"

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
	// Apparently only 8 and above works for "burst_size"

	// From the dpdk documentation, describing the rte_eth_rx_burst
	// function (and keeping in mind that their "nb_pkts" variable is the
	// same as our "burst size" variable below):
	// "Some drivers using vector instructions require that nb_pkts is
	// divisible by 4 or 8, depending on the driver implementation."

	constexpr int burst_size = 256;

	constexpr int expected_packet_size = 7188; // i.e., every packet that isn't the initial one
	constexpr uint32_t expected_packet_type = 0x291;

	constexpr int default_mbuf_size = 9000; // As opposed to RTE_MBUF_DEFAULT_BUF_SIZE

	constexpr int max_packets_to_dump = 10; 
	int dumped_packet_count           = 0;

	std::array<std::atomic<uint64_t>, NSTREAM> prev_timestamp_of_stream = {0};

	std::atomic<int> num_packets = 0;
	std::atomic<int> num_bytes   = 0;
	std::atomic<uint64_t> total_packets    = 0;
	std::atomic<uint64_t> non_ipv4_packets = 0;
	std::atomic<uint64_t> wrong_timestamp  = 0;

	std::map<uint64_t, std::atomic<size_t>> packets_per_stream;
	std::map<uint64_t, std::atomic<size_t>> seq_ids;

	std::ofstream datafile;
	const std::string output_data_filename = "dpdklibs_test_frame_receiver.dat";
} // namespace

static const struct rte_eth_conf port_conf_default = { 
	.rxmode = {
		.mtu = 9000,
		.offloads = (DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM),
	} 
};

static inline int
check_against_previous_stream(const detdataformats::DAQEthHeader* daq_header, uint64_t exp_ts_diff){
	uint64_t stream_id = daq_header->stream_id;
	uint64_t stream_ts = daq_header->timestamp;
	uint64_t seq_id    = daq_header->seq_id;
	int ret_val = 0;
	if ( prev_timestamp_of_stream[stream_id] == 0 ) {
		prev_timestamp_of_stream[stream_id] = stream_ts;
		return ret_val;
	}

	uint64_t ts_difference = stream_ts - prev_timestamp_of_stream[stream_id];
	if (ts_difference != exp_ts_diff) {
		TLOG() << fmt::format(
			"\nWARNING: wrong timestamp difference {} for stream {} (expected {})", 
			ts_difference, stream_id, exp_ts_diff
		);
		ret_val = 1;
	}

	uint64_t expected_seq_id = (seq_ids[stream_id] == 4095) ? 0 : seq_ids[stream_id]+1;
	if (seq_id != expected_seq_id) {
		TLOG() << fmt::format(
			"\nWARNING: sequence id for stream {} not consecutive. Previous Seq id: {}. Stream Seq id: {}, Expected Seq id: {}", 
			stream_id, seq_ids[stream_id], seq_id, expected_seq_id
		);
		ret_val += 2;
	}


	prev_timestamp_of_stream[stream_id] = stream_ts;
	return ret_val;
}

static int
lcore_main(struct rte_mempool* mbuf_pool){
	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */

	uint16_t port;

	RTE_ETH_FOREACH_DEV(port)
		if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != static_cast<int>(rte_socket_id())) {
			TLOG(TLVL_WARNING) << "WARNING, port " << port
				<< " is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n";
	}

	auto stats = std::thread([&]() {
		while (true) {
			TLOG() << fmt::format(
				"\nPackets/s: {} Bytes/s: {} Total packets: {} Non-IPV4 packets: {} Wrong Timestamp: {}\n",
				num_packets,
				num_bytes,
				total_packets,
				non_ipv4_packets,
				wrong_timestamp
			);

			std::string message = "";
			for (auto stream = packets_per_stream.begin(); stream != packets_per_stream.end(); stream++)
				message += fmt::format("\nTotal packets on stream {}: {}", stream->first, stream->second);
			TLOG() << message << "\n\n";
			std::cout<<"\n";
			num_packets.exchange(0);
			num_bytes.exchange(0);
			std::this_thread::sleep_for(std::chrono::seconds(1)); // If we sample for anything other than 1s,
		}                                                        // the rate calculation will need to change
	});

	struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
	rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

	datafile.open(output_data_filename, std::ios::out | std::ios::binary);
	if ( (datafile.rdstate() & std::ofstream::failbit ) != 0 ) {
		TLOG(TLVL_WARNING) << "Unable to open output file \"" << output_data_filename << "\"";
	}

	uint64_t udp_pkt_counter = 0;
	while (true) {

		RTE_ETH_FOREACH_DEV(port)
		{

			/* Get burst of RX packets, from first port of pair. */
			const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, burst_size);

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
				++udp_pkt_counter;

				char* message = udp::get_udp_payload(bufs[i_b]); //(char *)(udp_packet + 1);
				const detdataformats::DAQEthHeader* daq_header = reinterpret_cast<const detdataformats::DAQEthHeader*>(message);

				if ((udp_pkt_counter % 1000000) == 0 ) {
					std::cout<<"\n";
					TLOG() << "UDP HEADER:\n" << *daq_header;
					std::cout<<"\n";
				}

				uint64_t stream_id = daq_header->stream_id;
				if (packets_per_stream.find(stream_id) == packets_per_stream.end()) {
					packets_per_stream[stream_id] = 0;
					seq_ids[stream_id]       = daq_header->seq_id - 1;
				}
				
				if (check_against_previous_stream(daq_header, 2048) != 0){
					std::cout<<"\n";
					//TLOG() << udp_pkt_counter;
					//TLOG() << "UDP HEADER of wrong timestamp packet:\n" << *daq_header;
					dump_packet = true;
					wrong_timestamp++;
				}

				++packets_per_stream[stream_id];
				seq_ids[stream_id] = daq_header->seq_id;

				if (dump_packet && dumped_packet_count < max_packets_to_dump) {
					dumped_packet_count++;

					rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);
				}
			}

			rte_pktmbuf_free_bulk(bufs, nb_rx);
		}
	}
	return 0;
}

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void
signal_callback_handler(int signum){
	TLOG() << "Caught signal " << signum;
	if (datafile.is_open()) {
		datafile.close();
	}
	// Terminate program
	std::exit(signum);
}

int main(int argc, char** argv){
	//	define function to be called when ctrl+c is called.
	std::signal(SIGINT, signal_callback_handler);
	
	// initialise eal with argc and argv
	int ret = rte_eal_init(argc, argv);
	if (ret < 0) {rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");}

	auto nb_ports = rte_eth_dev_count_avail();
	TLOG() << "# of available ports: " << nb_ports;

	// Allocate pools and mbufs per queue
	std::map<int, std::unique_ptr<rte_mempool>> mbuf_pools;
	std::map<int, struct rte_mbuf **> bufs;
	uint16_t n_rx_qs = 1;

	TLOG() << "Allocating pools and mbufs.";
	for (size_t i=0; i<n_rx_qs; ++i) {
		std::stringstream ss;
		ss << "MBP-" << i;
		TLOG() << "Pool acquire: " << ss.str(); 
		mbuf_pools[i] = ealutils::get_mempool(ss.str());
		bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
		rte_pktmbuf_alloc_bulk(mbuf_pools[i].get(), bufs[i], burst_size);
	}

	// Setting up only port0
	TLOG() << "Initialize only port 0!";
	ealutils::port_init(0, n_rx_qs, 0, mbuf_pools); // just init port0, no TX queues

	lcore_main(mbuf_pools[0].get());

	rte_eal_cleanup();

	return 0;
}
