
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

#include <nlohmann/json.hpp>

#include <csignal>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include "detdataformats/DAQEthHeader.hpp"
#include "dpdklibs/FlowControl.hpp"
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
using json = nlohmann::json;

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
        std::string src_ip = "";

        std::atomic<uint64_t> total_packets              = 0;
        std::atomic<uint64_t> num_packets                = 0;
        std::atomic<uint64_t> num_bytes                  = 0;
        std::atomic<uint64_t> num_bad_timestamp          = 0;
        std::atomic<uint64_t> max_timestamp_skip         = 0;
        std::atomic<uint64_t> num_bad_seq_id             = 0;
        std::atomic<uint64_t> max_seq_id_skip            = 0;
        std::atomic<uint64_t> num_bad_payload_size       = 0;
        std::atomic<uint64_t> min_payload_size           = 0;
        std::atomic<uint64_t> max_payload_size           = 0;
        std::atomic<uint64_t> prev_seq_id                = 0;
        std::atomic<uint64_t> prev_timestamp             = 0;
        std::atomic<uint64_t> payload_size_bad_report    = 0;

        std::atomic<int64_t>  max_size_report_difference = 0;
        std::atomic<int64_t>  min_size_report_difference = 0;

        void reset() {
            num_packets         .exchange(0);
            num_bytes           .exchange(0);
            num_bad_timestamp   .exchange(0);
            num_bad_seq_id      .exchange(0);
            num_bad_payload_size.exchange(0);
        }
    };

    std::map<StreamUID, StreamStats> stream_stats;

    // Apparently only 8 and above works for "burst_size"

    // From the dpdk documentation, describing the rte_eth_rx_burst
    // function (and keeping in mind that their "nb_pkts" variable is the
    // same as our "burst size" variable below):
    // "Some drivers using vector instructions require that nb_pkts is
    // divisible by 4 or 8, depending on the driver implementation."

    unsigned master_lcore_id = 0; 

    bool check_timestamp    = false;
    bool per_stream_reports = false;

    std::string conf_filepath = "";

    constexpr int burst_size = 256;

    constexpr uint32_t expected_packet_type = 0x291;

    constexpr int default_mbuf_size = 9000; // As opposed to RTE_MBUF_DEFAULT_BUF_SIZE

    constexpr int max_packets_to_dump = 10; 
    int dumped_packet_count           = 0;

    uint16_t expected_payload_size = 0; //7243; // i.e., every packet that isn't the initial one

    struct CoreStats{
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
        std::atomic<uint64_t> payload_size_bad_report    = 0;

        std::atomic<int64_t>  max_size_report_difference = 0;
        std::atomic<int64_t>  min_size_report_difference = 0;

        void reset() {
            num_packets         .exchange(0);
            num_bytes           .exchange(0);
            num_bad_timestamp   .exchange(0);
            num_bad_seq_id      .exchange(0);
            num_bad_payload_size.exchange(0);
        }
    };

    //
    uint64_t time_per_report = 1;

    std::map<int, std::unique_ptr<rte_mempool>> mbuf_pools;
    std::map<int, struct rte_mbuf **> bufs;
    std::map<int, struct CoreStats> core_stats;

    uint16_t n_cores = 0;
    uint16_t n_rx_qs = 0;
    uint16_t iface   = 0;


    json conf;
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

static inline int check_against_previous_stream(const dunedaq::detdataformats::DAQEthHeader* daq_hdr, uint64_t exp_ts_diff, int lcore_id){
    // uint64_t unique_str_id = (daq_hdr->det_id<<22) + (daq_hdr->crate_id<<12) + (daq_hdr->slot_id<<8) + daq_hdr->stream_id;  
    StreamUID unique_str_id = {daq_hdr->det_id, daq_hdr->crate_id, daq_hdr->slot_id, daq_hdr->stream_id};  
    uint64_t stream_ts     = daq_hdr->timestamp;
    uint64_t seq_id        = daq_hdr->seq_id;
    int ret_val = 0;

    if (check_timestamp) {
        if (stream_stats[unique_str_id].prev_timestamp == 0 ) {
            stream_stats[unique_str_id].prev_timestamp = stream_ts;
        }else{
            uint64_t expected_ts   = stream_stats[unique_str_id].prev_timestamp + exp_ts_diff;
            if (stream_ts != expected_ts) {
                uint64_t ts_difference = stream_ts - stream_stats[unique_str_id].prev_timestamp;
                ret_val = 1;
                ++core_stats[lcore_id].num_bad_timestamp;
                ++stream_stats[unique_str_id].num_bad_timestamp;
                ++core_stats[lcore_id].total_bad_timestamp;
                if (ts_difference > core_stats[lcore_id].max_timestamp_skip) {core_stats[lcore_id].max_timestamp_skip = ts_difference;}
                if (ts_difference > stream_stats[unique_str_id].max_timestamp_skip) {stream_stats[unique_str_id].max_timestamp_skip = ts_difference;}
            }
            stream_stats[unique_str_id].prev_timestamp = stream_ts;
        }
    }


    uint64_t expected_seq_id = (stream_stats[unique_str_id].prev_seq_id == 4095) ? 0 : stream_stats[unique_str_id].prev_seq_id + 1;
    if (seq_id != expected_seq_id) {
        uint64_t adj_expected_seq_id = (expected_seq_id == 0) ? 4096 : expected_seq_id;
        uint64_t adj_seq_id          = (seq_id < adj_expected_seq_id) ? (4096 + seq_id) : seq_id;
        uint64_t seq_id_difference   = adj_seq_id - adj_expected_seq_id;
        ret_val += 2;
        ++core_stats[lcore_id].num_bad_seq_id;
        ++core_stats[lcore_id].total_bad_seq_id;
        ++stream_stats[unique_str_id].num_bad_seq_id;
        if (seq_id_difference > core_stats[lcore_id].max_seq_id_skip) {core_stats[lcore_id].max_seq_id_skip = seq_id_difference;}
        if (seq_id_difference > stream_stats[unique_str_id].max_seq_id_skip) {stream_stats[unique_str_id].max_seq_id_skip = seq_id_difference;}
    }

    return ret_val;
}

static inline int check_payload_size(struct rte_mbuf* mbuf, StreamUID unique_str_id, int lcore_id){
    std::size_t payload_size = mbuf->pkt_len;
    std::size_t header_size = sizeof(dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr);
    payload_size -= header_size;

    std::size_t reported_payload_size = dunedaq::dpdklibs::udp::get_payload_size_mbuf(mbuf);

    size_t size_difference = payload_size - reported_payload_size;
    if (size_difference) {
        ++core_stats[lcore_id].payload_size_bad_report;
        ++stream_stats[unique_str_id].payload_size_bad_report;
        if (size_difference > core_stats[lcore_id].max_size_report_difference) {core_stats[lcore_id].max_size_report_difference = size_difference;}
        if (size_difference < core_stats[lcore_id].min_size_report_difference) {core_stats[lcore_id].min_size_report_difference = size_difference;}
        if (size_difference > stream_stats[unique_str_id].max_size_report_difference) {stream_stats[unique_str_id].max_size_report_difference = size_difference;}
        if (size_difference < stream_stats[unique_str_id].min_size_report_difference) {stream_stats[unique_str_id].min_size_report_difference = size_difference;}
    }
    if (payload_size > core_stats[lcore_id].max_payload_size) {core_stats[lcore_id].max_payload_size = payload_size;}
    if ((payload_size < core_stats[lcore_id].min_payload_size) or (core_stats[lcore_id].min_payload_size == 0)) {core_stats[lcore_id].min_payload_size = payload_size;}
    if (payload_size > stream_stats[unique_str_id].max_payload_size) {stream_stats[unique_str_id].max_payload_size = payload_size;}
    if ((payload_size < stream_stats[unique_str_id].min_payload_size) or (stream_stats[unique_str_id].min_payload_size == 0)) {stream_stats[unique_str_id].min_payload_size = payload_size;}

    if (expected_payload_size and (payload_size != expected_payload_size)){
        ++core_stats[lcore_id].num_bad_payload_size;
        ++core_stats[lcore_id].total_bad_payload_size;
        ++stream_stats[unique_str_id].num_bad_payload_size;
        return 1;
    }
    return 0;
}

static int lcore_main(void* _unused){
    /*
     * Check that the iface is on the same NUMA node as the polling thread
     * for best performance.
     */
    int lcore_id = rte_lcore_id();

    if (rte_eth_dev_socket_id(iface) >= 0 && rte_eth_dev_socket_id(iface) != static_cast<int>(rte_socket_id())) {
        fmt::print(
            "WARNING, iface {} is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", iface
        );
    }


    while (true) {
        for (auto& q : conf[std::to_string(iface)][std::to_string(lcore_id)].items()) {
            int q_id = stoi(q.key());
            /* Get burst of RX packets, from first iface of pair. */
            const uint16_t nb_rx = rte_eth_rx_burst(iface, q_id, bufs[q_id], burst_size);

            core_stats[lcore_id].num_packets   += nb_rx;
            core_stats[lcore_id].total_packets += nb_rx;

            for (int i_b = 0; i_b < nb_rx; ++i_b) {
                core_stats[lcore_id].num_bytes += bufs[q_id][i_b]->pkt_len;

                bool dump_packet = false;
                if (not RTE_ETH_IS_IPV4_HDR(bufs[q_id][i_b]->packet_type)) {
                    core_stats[lcore_id].non_ipv4_packets++;
                    dump_packet = true;
                    continue;
                }
                ++core_stats[lcore_id].udp_pkt_counter;

                char* udp_payload = dunedaq::dpdklibs::udp::get_udp_payload(bufs[q_id][i_b]);
                const dunedaq::detdataformats::DAQEthHeader* daq_hdr = reinterpret_cast<const dunedaq::detdataformats::DAQEthHeader*>(udp_payload);
                if (daq_hdr == nullptr){
                    fmt::print("IT IS A NULL PTR\n");
                }
                //printing daq hdr
                //fmt::print("det id: {}, crate id {}, slot id {}, stream id: {}\n", daq_hdr->det_id, daq_hdr->crate_id, daq_hdr->slot_id, daq_hdr->stream_id);
                StreamUID unique_str_id = {daq_hdr->det_id, daq_hdr->crate_id, daq_hdr->slot_id, daq_hdr->stream_id};
                if ((core_stats[lcore_id].udp_pkt_counter % 1000000) == 0 ) {
                    std::cout << "\nDAQ HEADER:\n" << *daq_hdr<< "\n";
                }
                if (stream_stats.count(unique_str_id) == 0) {
                    stream_stats[unique_str_id];
                    struct dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr * pkt = rte_pktmbuf_mtod(bufs[q_id][i_b], struct dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr *);
                    stream_stats[unique_str_id].src_ip = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(
                        dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.src_addr))
                    );
                    stream_stats[unique_str_id].prev_seq_id = daq_hdr->seq_id - 1;
                }
                stream_stats[unique_str_id].num_bytes += bufs[q_id][i_b]->pkt_len;

                if (check_against_previous_stream(daq_hdr, 2048, lcore_id) != 0){
                    dump_packet = true;
                }
                if (check_payload_size(bufs[q_id][i_b], unique_str_id, lcore_id) != 0){
                    dump_packet = true;
                }

                ++stream_stats[unique_str_id].total_packets;
                ++stream_stats[unique_str_id].num_packets;
                stream_stats[unique_str_id].prev_seq_id = daq_hdr->seq_id;

                if (dump_packet && dumped_packet_count < max_packets_to_dump) {
                    dumped_packet_count++;
                    //rte_pktmbuf_dump(stdout, bufs[q_id][i_b], bufs[q_id][i_b]->pkt_len);
                }
            }

            rte_pktmbuf_free_bulk(bufs[q_id], nb_rx);
        }
    }

    return 0;
}

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum){
    fmt::print("Caught signal {}\n", signum);
    // Terminate program
    std::exit(signum);
}

int main(int argc, char** argv){
    CLI::App app{"test frame receiver"};
    app.add_option("-s", expected_payload_size, "Expected frame size");
    app.add_option("-i", iface, "Interface to init");
    app.add_option("-t", time_per_report, "Time Per Report");
    app.add_option("-c,conf", conf_filepath, "configuration Json file path");
    app.add_option("-m", master_lcore_id, "configure master lcore");
    app.add_flag("--check-time", check_timestamp, "Report back differences in timestamp");
    app.add_flag("-p", per_stream_reports, "Detailed per stream reports");
    CLI11_PARSE(app, argc, argv);

    fmt::print("FILEPATH {}\n", conf_filepath);
    std::ifstream f(conf_filepath);
    conf = json::parse(f);
    //    define function to be called when ctrl+c is called.
    std::signal(SIGINT, signal_callback_handler);
    
    std::vector<std::string> eal_args;
    eal_args.push_back("dpdklibds_test_frame_receiver");
    eal_args.push_back("--main-lcore");
    eal_args.push_back(std::to_string(master_lcore_id));


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

    fmt::print("Looking through conf file\n");
    std::map <std::string, int> queue_multiplicity;
    bool failiure = false;

    for (auto& conf_iface: conf) {
        for (auto& lcore: conf_iface.items()) {
            if (stoi(lcore.key()) == master_lcore_id){
                fmt::print("ERROR: conf file includes main lcore {}. Change conf file or change main lcore with -m\n", lcore.key());
                failiure = true;
            }
            ++n_cores;
            for (auto& q: lcore.value().items()) {
                ++queue_multiplicity[q.key()];
                if (queue_multiplicity[q.key()] > 1){
                    fmt::print("ERROR: queue {} repeated in conf file\n", q.key());
                    failiure = true;
                }
                    ++n_rx_qs;
            }
        }
    }
    if (failiure) {std::exit(1);}

    // Allocate pools and mbufs per queue
    std::cout << "Allocating pools and mbufs.\n";
    for (size_t i=0; i<n_rx_qs; ++i) {
        std::stringstream ss;
        ss << "MBP-" << i;
        fmt::print("Pool acquire: {}\n", ss.str()); 
        mbuf_pools[i] = dunedaq::dpdklibs::ealutils::get_mempool(ss.str());
        bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
        rte_pktmbuf_alloc_bulk(mbuf_pools[i].get(), bufs[i], burst_size);
    }

    // Setting up only one iface
    fmt::print("Initialize only iface {}!\n", iface);
    dunedaq::dpdklibs::ealutils::iface_init(iface, n_rx_qs, 0, mbuf_pools); // just init iface, no TX queues
    // Promiscuous mode
    dunedaq::dpdklibs::ealutils::iface_promiscuous_mode(iface, false); // should come from config

    // // Flow steering
    struct rte_flow_error error;
    struct rte_flow *flow;
    // flushing previous flow steering rules
    rte_flow_flush(iface, &error);
    for (auto const& rxqs : conf[std::to_string(iface)]) {
        for (auto const& rxq : rxqs.items()) {
            int rxqid = stoi(rxq.key());
            std::string srcip = rxq.value();
            // Put the IP numbers temporarily in a vector, so they can be converted easily to uint32_t
            size_t ind = 0, current_ind = 0;
            std::vector<uint8_t> v;
            for (int i = 0; i < 4; ++i) {
                v.push_back(std::stoi(srcip.substr(current_ind, srcip.size() - current_ind), &ind));
                current_ind += ind + 1;
            }

            flow = dunedaq::dpdklibs::generate_ipv4_flow(iface, rxqid,
            RTE_IPV4(v[0], v[1], v[2], v[3]), 0xffffffff, 0, 0, &error);

            if (not flow) { // ers::fatal
                fmt::print(
                    "Flow can't be created for {}, Error type: {}, Message: {}\n", 
                    rxqid, (unsigned)error.type, error.message
                );
                rte_exit(EXIT_FAILURE, "error in creating flow");
            }
        }
    }

    fmt::print("Let's loop\n");
    for (auto& conf_iface: conf) {
        for (auto& lcore: conf_iface.items()) {
            core_stats[stoi(lcore.key())];
            rte_eal_remote_launch(lcore_main, NULL, stoi(lcore.key()));
        }
    }
    // for (size_t i=1; i<=n_cores; ++i) {
    //     core_stats[i];
    //     rte_eal_remote_launch(lcore_main, NULL, i);
    // }

    // reporting thread
    auto stats = std::thread([&]() {
        fmt::print("in thread\n");

        while (true) {
            uint64_t summed_packets_per_second          = 0;
            float    summed_bytes_per_second            = 0;
            uint64_t summed_total_packets               = 0;
            uint64_t summed_non_ipv4_packets            = 0;
            uint64_t summed_udp_pkt_counter             = 0;
            uint64_t summed_num_bad_seq_id              = 0;
            uint64_t summed_max_seq_id_skip             = 0;
            uint64_t summed_total_bad_seq_id            = 0;
            uint64_t summed_max_payload_size            = 0;
            uint64_t summed_min_payload_size            = 0;
            uint64_t summed_num_bad_payload_size        = 0;
            uint64_t summed_total_bad_payload_size      = 0;
            uint64_t summed_payload_size_bad_report     = 0;
            uint64_t summed_min_size_report_difference  = 0;
            uint64_t summed_max_size_report_difference  = 0;
            uint64_t summed_num_bad_timestamp           = 0;
            uint64_t summed_max_timestamp_skip          = 0;
            uint64_t summed_total_bad_timestamp         = 0;
            for (auto& [lcore_id, stats] : core_stats) {
                summed_packets_per_second += stats.num_packets / time_per_report;
                summed_bytes_per_second   += (float)stats.num_bytes / (1024.*1024.*1024.) / (float)time_per_report;

                summed_total_packets           += stats.total_packets;
                summed_non_ipv4_packets        += stats.non_ipv4_packets;
                summed_udp_pkt_counter         += stats.udp_pkt_counter;
                summed_num_bad_seq_id          += stats.num_bad_seq_id;
                summed_total_bad_seq_id        += stats.total_bad_seq_id;
                summed_num_bad_payload_size    += stats.num_bad_payload_size;
                summed_total_bad_payload_size  += stats.total_bad_payload_size;
                summed_payload_size_bad_report += stats.payload_size_bad_report;
                summed_num_bad_timestamp       += stats.num_bad_timestamp;
                summed_total_bad_timestamp     += stats.total_bad_timestamp;

                if (summed_max_seq_id_skip            < stats.max_seq_id_skip           ){
                    summed_max_seq_id_skip            = stats.max_seq_id_skip;
                }
                if (summed_max_payload_size           < stats.max_payload_size          ){
                    summed_max_payload_size           = stats.max_payload_size;
                }
                if (((summed_min_payload_size > stats.min_payload_size) and (stats.min_payload_size != 0)) or (summed_min_payload_size == 0)){
                    summed_min_payload_size           = stats.min_payload_size;
                }
                if (((summed_min_size_report_difference > stats.min_size_report_difference) and (stats.min_size_report_difference != 0)) or (summed_min_size_report_difference == 0){
                    summed_min_size_report_difference = stats.min_size_report_difference;
                }
                if (summed_max_size_report_difference < stats.max_size_report_difference){
                    summed_max_size_report_difference = stats.max_size_report_difference;
                }
                if (summed_max_timestamp_skip         < stats.max_timestamp_skip        ){
                    summed_max_timestamp_skip         = stats.max_timestamp_skip;
                }

                stats.reset();
            }



            fmt::print(
                "Since the last report {} seconds ago:\n"
                "Packets/s: {} ({:8.3f} GB/s), Total packets: {} Non-IPV4 packets: {} Total UDP packets: {}\n"
                "Packets with wrong sequence id: {}, Max seq_id skip: {}, Total Packets with Wrong seq_id {}\n"
                "Max udp payload: {}, Min udp payload: {}\n",
                time_per_report,
                summed_packets_per_second, summed_bytes_per_second, summed_total_packets, summed_non_ipv4_packets, summed_udp_pkt_counter,
                summed_num_bad_seq_id, summed_max_seq_id_skip, summed_total_bad_seq_id,
                summed_max_payload_size, summed_min_payload_size
            );

            if (expected_payload_size){
                fmt::print(
                    "Packets with wrong payload size: {}, Total Packets with Wrong size: {}\n"
                    "Total packets where the reported size differs from the actual size: {}, min/max difference: {} / {}\n",
                    summed_num_bad_payload_size, summed_total_bad_payload_size,
                    summed_payload_size_bad_report, summed_min_size_report_difference, summed_max_size_report_difference
                );
            }

            if (check_timestamp) {
                fmt::print(
                    "Wrong Timestamp difference Packets: {}, Max wrong Timestamp difference {}, Total Packets with Wrong Timestamp {}\n",
                    summed_num_bad_timestamp, summed_max_timestamp_skip, summed_total_bad_timestamp
                );
            }

            fmt::print("\n");
            for (auto& [suid, stats] : stream_stats) {
                fmt::print("Stream {:18}: pkts {} (tot. {})  ", (std::string)suid, stats.num_packets, stats.total_packets);
                if (per_stream_reports){
                    float stream_bytes_per_second = (float)stats.num_bytes / (1024.*1024.*1024.) / (float)time_per_report;
                    fmt::print(
                        "{:8.3f} GB/s,    seq_id jumps: {},    max skip: {} ", 
                        stream_bytes_per_second, stats.num_bad_seq_id, stats.max_seq_id_skip
                    );
                    if (check_timestamp){
                        fmt::print(
                            "Packets with wrong timestamp: {}, max timestamp skip: {} ",
                            stats.num_bad_timestamp, stats.max_timestamp_skip
                        );
                    }
                    if (expected_payload_size){
                        fmt::print(
                            "Packets with wrong payload size: {}, min/max payload size: {} / {} "
                            "Total packets where the reported size differs from the actual size: {}, min/max difference: {} / {} ",
                            stats.num_bad_payload_size, stats.min_payload_size, stats.max_payload_size,
                            stats.payload_size_bad_report, stats.min_size_report_difference, stats.max_size_report_difference
                        );
                    }
                }
                fmt::print("\n");
                stats.reset();
            }

            fmt::print("\n");
            std::this_thread::sleep_for(std::chrono::seconds(time_per_report));
        }
    });
    rte_eal_cleanup();

    return 0;
}
