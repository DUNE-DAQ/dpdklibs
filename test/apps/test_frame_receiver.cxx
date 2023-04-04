
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

    struct StreamUID {
        uint64_t det_id : 6;
        uint64_t crate_id : 10;
        uint64_t slot_id : 4;
        uint64_t stream_id : 8;

        bool operator<(const StreamUID& rhs) const
        {
            // compares n to rhs.n,
            // then s to rhs.s,
            // then d to rhs.d
            return std::tie(det_id, crate_id, slot_id, stream_id) < std::tie(rhs.det_id, rhs.crate_id, rhs.slot_id, rhs.stream_id);
        }

        operator std::string() const {
            return fmt::format("({}, {}, {}, {})", det_id, crate_id, slot_id, stream_id);
        }
    };

    std::ostream & operator <<(std::ostream &out, const StreamUID &obj) {
        return out << static_cast<std::string>(obj);
    }

    struct StreamStats {
        std::atomic<uint64_t> num_packets            = 0;
        std::atomic<uint64_t> num_bytes              = 0;
        std::atomic<uint64_t> num_bad_timestamp      = 0;
        std::atomic<uint64_t> max_timestamp_skip     = 0;
        std::atomic<uint64_t> num_bad_seq_id         = 0;
        std::atomic<uint64_t> max_seq_id_skip        = 0;
        std::atomic<uint64_t> num_bad_payload_size   = 0;
        std::atomic<uint64_t> min_payload_size       = 0;
        std::atomic<uint64_t> max_payload_size       = 0;

        void reset() {
            num_packets.exchange(0);
            num_bytes.exchange(0);
            num_bad_timestamp.exchange(0);
            max_timestamp_skip.exchange(0);
            num_bad_seq_id.exchange(0);
            max_seq_id_skip.exchange(0);
            num_bad_payload_size.exchange(0);
            min_payload_size.exchange(0);
            max_payload_size.exchange(0);
        }
    };

    // Apparently only 8 and above works for "burst_size"

    // From the dpdk documentation, describing the rte_eth_rx_burst
    // function (and keeping in mind that their "nb_pkts" variable is the
    // same as our "burst size" variable below):
    // "Some drivers using vector instructions require that nb_pkts is
    // divisible by 4 or 8, depending on the driver implementation."

    bool check_timestamp = false;

    constexpr int burst_size = 256;

    constexpr uint32_t expected_packet_type = 0x291;

    constexpr int default_mbuf_size = 9000; // As opposed to RTE_MBUF_DEFAULT_BUF_SIZE

    constexpr int max_packets_to_dump = 10; 
    int dumped_packet_count           = 0;

    uint16_t expected_packet_size = 0; //7243; // i.e., every packet that isn't the initial one

    std::atomic<uint64_t> num_packets            = 0;
    std::atomic<uint64_t> num_bytes              = 0;
    std::atomic<uint64_t> total_packets          = 0;
    std::atomic<uint64_t> non_ipv4_packets       = 0;
    std::atomic<uint64_t> total_bad_timestamp    = 0;
    std::atomic<uint64_t> num_bad_timestamp      = 0;
    std::atomic<uint64_t> max_timestamp_skip     = 0;
    std::atomic<uint64_t> total_bad_seq_id       = 0;
    std::atomic<uint64_t> num_bad_seq_id         = 0;
    std::atomic<uint64_t> max_seq_id_skip        = 0;
    std::atomic<uint64_t> total_bad_payload_size = 0;
    std::atomic<uint64_t> num_bad_payload_size   = 0;
    std::atomic<uint64_t> min_payload_size       = 0;
    std::atomic<uint64_t> max_payload_size       = 0;
    std::atomic<uint64_t> udp_pkt_counter        = 0;

    // std::map<uint64_t, std::atomic<size_t>> packets_per_stream;
    // std::map<uint64_t, std::atomic<size_t>> prev_timestamp_of_stream;
    // std::map<uint64_t, std::atomic<size_t>> seq_ids;

    std::map<StreamUID, std::atomic<size_t>> packets_per_stream;
    std::map<StreamUID, std::atomic<size_t>> prev_timestamp_of_stream;
    std::map<StreamUID, std::atomic<size_t>> seq_ids;

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

static inline int check_against_previous_stream(const detdataformats::DAQEthHeader* daq_hdr, uint64_t exp_ts_diff){
    // uint64_t unique_str_id = (daq_hdr->det_id<<22) + (daq_hdr->crate_id<<12) + (daq_hdr->slot_id<<8) + daq_hdr->stream_id;  
    StreamUID unique_str_id = {daq_hdr->det_id, daq_hdr->crate_id, daq_hdr->slot_id, daq_hdr->stream_id};  
    uint64_t stream_ts     = daq_hdr->timestamp;
    uint64_t seq_id        = daq_hdr->seq_id;
    int ret_val = 0;

    if (check_timestamp) {
        if ( prev_timestamp_of_stream[unique_str_id] == 0 ) {
            prev_timestamp_of_stream[unique_str_id] = stream_ts;
        }else{
            uint64_t expected_ts   = prev_timestamp_of_stream[unique_str_id] + exp_ts_diff;
            if (stream_ts != expected_ts) {
                uint64_t ts_difference = stream_ts - prev_timestamp_of_stream[unique_str_id];
                ret_val = 1;
                ++num_bad_timestamp;
                ++total_bad_timestamp;
                if (ts_difference > max_timestamp_skip) {max_timestamp_skip = ts_difference;}
            }
            prev_timestamp_of_stream[unique_str_id] = stream_ts;
        }
    }


    uint64_t expected_seq_id = (seq_ids[unique_str_id] == 4095) ? 0 : seq_ids[unique_str_id]+1;
    if (seq_id != expected_seq_id) {
        uint64_t adj_expected_seq_id = (expected_seq_id == 0) ? 4096 : expected_seq_id;
        uint64_t adj_seq_id          = (seq_id < adj_expected_seq_id) ? (4096 + seq_id) : seq_id;
        uint64_t seq_id_difference   = adj_seq_id - adj_expected_seq_id;
        ret_val += 2;
        ++num_bad_seq_id;
        ++total_bad_seq_id;
        if (seq_id_difference > max_seq_id_skip) {max_seq_id_skip = seq_id_difference;}
    }

    return ret_val;
}

static inline int check_packet_size(struct rte_mbuf* mbuf){
    std::size_t packet_size = mbuf->data_len;
    // std::size_t packet_size_from_sizeof = sizeof(udp::get_udp_payload(mbuf));
    // if (packet_size != packet_size_from_sizeof){
    //     std::cout << fmt::format("WARNING: sizeof and header give different results. sizeof: {}, header: {}\n", packet_size_from_sizeof, packet_size_from_header);
    // }

    if (packet_size > max_payload_size) {max_payload_size = packet_size;}
    if (packet_size < min_payload_size) {min_payload_size = packet_size;}

    if (expected_packet_size and (packet_size != expected_packet_size)){
        ++num_bad_payload_size;
        ++total_bad_payload_size;
        return 1;
    }
    return 0;
}

static int lcore_main(struct rte_mempool* mbuf_pool, uint16_t iface, uint64_t time_per_report){
    /*
     * Check that the iface is on the same NUMA node as the polling thread
     * for best performance.
     */

    if (rte_eth_dev_socket_id(iface) >= 0 && rte_eth_dev_socket_id(iface) != static_cast<int>(rte_socket_id())) {
        std::cout << fmt::format(
            "WARNING, iface {} is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", iface
        );
    }

    auto stats = std::thread([&]() {
        while (true) {
            uint64_t packets_per_second = num_packets / time_per_report;
            uint64_t bytes_per_second   = num_bytes   / time_per_report;
            std::cout << fmt::format(
                "Since the last report {} seconds ago:\n"
                "Packets/s: {} Bytes/s: {} Total packets: {} Non-IPV4 packets: {} Total UDP packets: {}\n"
                "Packets with wrong sequence id: {}, Max wrong seq_id jump {}, Total Packets with Wrong seq_id {}\n"
                "Max udp payload: {}, Min udp payload: {}\n",
                time_per_report,
                packets_per_second, bytes_per_second, total_packets, non_ipv4_packets, udp_pkt_counter,
                num_bad_seq_id, max_seq_id_skip, total_bad_seq_id,
                max_payload_size, min_payload_size
            );

            if (expected_packet_size){
                std::cout << fmt::format(
                    "Packets with wrong payload size: {}, Total Packets with Wrong size {}\n",
                    num_bad_payload_size, total_bad_payload_size
                );
            }

            if (check_timestamp) {
                std::cout << fmt::format(
                    "Wrong Timestamp difference Packets: {}, Max wrong Timestamp difference {}, Total Packets with Wrong Timestamp {}\n",
                    num_bad_timestamp, max_timestamp_skip, total_bad_timestamp
                );
            }

            std::string message = "";
            for (auto stream = packets_per_stream.begin(); stream != packets_per_stream.end(); stream++)
                message += fmt::format("\nTotal packets on stream {}: {}", (std::string)stream->first, stream->second);


            std::cout << message << "\n\n";
            num_packets.exchange(0);
            num_bytes.exchange(0);
            num_bad_timestamp.exchange(0);
            num_bad_seq_id.exchange(0);
            num_bad_payload_size.exchange(0);
            max_payload_size.exchange(0);
            min_payload_size.exchange(0);
            max_seq_id_skip.exchange(0);
            max_timestamp_skip.exchange(0);
            std::this_thread::sleep_for(std::chrono::seconds(time_per_report));
        }
    });

    struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
    rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

    datafile.open(output_data_filename, std::ios::out | std::ios::binary);
    if ( (datafile.rdstate() & std::ofstream::failbit ) != 0 ) {
        std::cout <<  fmt::format("WARNING: Unable to open output file \"{}\"\n", output_data_filename);
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
            ++udp_pkt_counter;

            char* udp_payload = udp::get_udp_payload(bufs[i_b]);
            const detdataformats::DAQEthHeader* daq_hdr = reinterpret_cast<const detdataformats::DAQEthHeader*>(udp_payload);

            // uint64_t unique_str_id = (daq_hdr->det_id<<22) + (daq_hdr->crate_id<<12) + (daq_hdr->slot_id<<8) + daq_hdr->stream_id;
            // uint64_t unique_str_id = (daq_hdr->det_id<<22) + (daq_hdr->crate_id<<12) + (daq_hdr->slot_id<<8) + daq_hdr->stream_id;
            StreamUID unique_str_id = {daq_hdr->det_id, daq_hdr->crate_id, daq_hdr->slot_id, daq_hdr->stream_id};
            if ((udp_pkt_counter % 1000000) == 0 ) {
                std::cout << "\nDAQ HEADER:\n" << *daq_hdr<< "\n";
            }
            if (packets_per_stream.find(unique_str_id) == packets_per_stream.end()) {
                packets_per_stream[unique_str_id] = 0;
                seq_ids[unique_str_id]       = daq_hdr->seq_id - 1;
                if (check_timestamp) {
                    prev_timestamp_of_stream[unique_str_id] = 0;
                }
            }

            if (check_against_previous_stream(daq_hdr, 2048) != 0){
                dump_packet = true;
            }
            if (check_packet_size(bufs[i_b]) != 0){
                dump_packet = true;
            }

            ++packets_per_stream[unique_str_id];
            seq_ids[unique_str_id] = daq_hdr->seq_id;

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
    std::cout << fmt::format("Caught signal {}\n", signum);
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
    CLI11_PARSE(app, argc, argv);

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
    std::cout << fmt::format("# of available ifaces: {}\n", n_ifaces);
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
        std::cout << fmt::format("Pool acquire: {}\n", ss.str()); 
        mbuf_pools[i] = ealutils::get_mempool(ss.str());
        bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
        rte_pktmbuf_alloc_bulk(mbuf_pools[i].get(), bufs[i], burst_size);
    }

    // Setting up only one iface
    std::cout << fmt::format("Initialize only iface {}!\n", iface);
    ealutils::iface_init(iface, n_rx_qs, 0, mbuf_pools); // just init iface, no TX queues

    lcore_main(mbuf_pools[0].get(), iface, time_per_report);

    rte_eal_cleanup();

    return 0;
}
