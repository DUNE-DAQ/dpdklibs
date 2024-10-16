
/* Application will run until quit or killed. */

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
#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "logging/Logging.hpp"

#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/arp/ARP.hpp"
#include "dpdklibs/ipv4_addr.hpp"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <regex>

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

    std::ostream & operator <<(std::ostream &out, const StreamUID &obj) {
        return out << static_cast<std::string>(obj);
    }

    struct StreamStats {
        std::atomic<uint64_t> total_packets          = 0;
        std::atomic<uint64_t> num_packets            = 0;
        std::atomic<uint64_t> num_bytes              = 0;
        std::atomic<uint64_t> num_bad_timestamp      = 0;
        std::atomic<uint64_t> max_timestamp_skip     = 0;
        std::atomic<uint64_t> num_bad_seq_id         = 0;
        std::atomic<uint64_t> max_seq_id_skip        = 0;
        std::atomic<uint64_t> num_bad_payload_size   = 0;
        std::atomic<uint64_t> min_payload_size       = 0;
        std::atomic<uint64_t> max_payload_size       = 0;
        std::atomic<uint64_t> prev_seq_id            = 0;
        std::atomic<uint64_t> prev_timestamp         = 0;

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

    std::map<StreamUID, StreamStats> stream_stats;

    // Apparently only 8 and above works for "burst_size"

    // From the dpdk documentation, describing the rte_eth_rx_burst
    // function (and keeping in mind that their "nb_pkts" variable is the
    // same as our "burst size" variable below):
    // "Some drivers using vector instructions require that nb_pkts is
    // divisible by 4 or 8, depending on the driver implementation."

    bool check_timestamp = false;
    bool per_stream_reports         = false;

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

      std::atomic<int64_t> garps_sent = 0;

    // std::ofstream datafile;
    // const std::string output_data_filename = "dpdklibs_test_frame_receiver.dat";


} // namespace

std::vector<char*> construct_argv(std::vector<std::string> &std_argv){
    std::vector<char*> vec_argv;
    for (int i=0; i < std_argv.size() ; i++){
        vec_argv.insert(vec_argv.end(), std_argv[i].data());
    }
    return vec_argv;
}

static inline int check_against_previous_stream(const detdataformats::DAQEthHeader* daq_hdr, uint64_t exp_ts_diff){
    // uint64_t unique_str_id = (daq_hdr->det_id<<22) + (daq_hdr->crate_id<<12) + (daq_hdr->slot_id<<8) + daq_hdr->stream_id;  
  StreamUID unique_str_id(*daq_hdr);
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
                ++num_bad_timestamp;
                ++stream_stats[unique_str_id].num_bad_timestamp;
                ++total_bad_timestamp;
                if (ts_difference > max_timestamp_skip) {max_timestamp_skip = ts_difference;}
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
        ++num_bad_seq_id;
        ++total_bad_seq_id;
        ++stream_stats[unique_str_id].num_bad_seq_id;
        if (seq_id_difference > max_seq_id_skip) {max_seq_id_skip = seq_id_difference;}
        if (seq_id_difference > stream_stats[unique_str_id].max_seq_id_skip) {stream_stats[unique_str_id].max_seq_id_skip = seq_id_difference;}
    }

    return ret_val;
}

static inline int check_packet_size(struct rte_mbuf* mbuf, StreamUID unique_str_id){
    std::size_t packet_size = mbuf->data_len;

    if (packet_size > max_payload_size) {max_payload_size = packet_size;}
    if (packet_size < min_payload_size) {min_payload_size = packet_size;}
    if (packet_size > stream_stats[unique_str_id].max_payload_size) {stream_stats[unique_str_id].max_payload_size = packet_size;}
    if (packet_size < stream_stats[unique_str_id].min_payload_size) {stream_stats[unique_str_id].min_payload_size = packet_size;}

    if (expected_packet_size and (packet_size != expected_packet_size)){
        ++num_bad_payload_size;
        ++total_bad_payload_size;
        ++stream_stats[unique_str_id].num_bad_payload_size;
        return 1;
    }
    return 0;
}

static int lcore_main(struct rte_mempool* mbuf_pool, uint16_t iface, uint64_t time_per_report, const std::vector<std::string>& garp_ip_addr_strs){
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
        while (true) {
            uint64_t packets_per_second = num_packets / time_per_report;
            uint64_t bytes_per_second   = num_bytes   / time_per_report;
            fmt::print(
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
                fmt::print(
                    "Packets with wrong payload size: {}, Total Packets with Wrong size {}\n",
                    num_bad_payload_size, total_bad_payload_size
                );
            }

            if (check_timestamp) {
                fmt::print(
                    "Wrong Timestamp difference Packets: {}, Max wrong Timestamp difference {}, Total Packets with Wrong Timestamp {}\n",
                    num_bad_timestamp, max_timestamp_skip, total_bad_timestamp
                );
            }

            fmt::print("\n");
            // for (auto stream = stream_stats.begin(); stream != stream_stats.end(); stream++) {
            for ( auto& [suid, stats] : stream_stats) {
                fmt::print("Stream {:15}: n.pkts {} (tot. {})", (std::string)suid, stats.num_packets, stats.total_packets);
                if (per_stream_reports){
                    float stream_bytes_per_second = (float)stats.num_bytes / (1024.*1024.) / (float)time_per_report;
                    fmt::print(
                        " {:8.3f} MB/s, seq. jumps: {}", 
                        stream_bytes_per_second, stats.num_bad_seq_id
                    );
                }
                stats.reset();
                fmt::print("\n");
            }

            fmt::print("\n");
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

    //
    // Prepare and start GARP thread
    //
    std::vector<rte_be32_t> ip_addr_bin_vector;
    for( const auto& ip_addr_str : garp_ip_addr_strs ) {
    TLOG() << "IP address for ARP responses: " << ip_addr_str;
        IpAddr ip_addr(ip_addr_str);
        rte_be32_t ip_addr_bin = ip_address_dotdecimal_to_binary(
        ip_addr.addr_bytes[3],
        ip_addr.addr_bytes[2],
        ip_addr.addr_bytes[1],
        ip_addr.addr_bytes[0]
        );
        ip_addr_bin_vector.push_back(ip_addr_bin);
    }

    struct rte_mbuf **tx_bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
    rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, burst_size);

    auto garp = std::thread([&]() {
        while (true) {
        // TLOG() << "Packets/s: " << num_packets << " Bytes/s: " << num_bytes << " Total packets: " << total_packets << " Failed packets: " << failed_packets;
        // num_packets.exchange(0);
        // num_bytes.exchange(0);

        for(const auto& ip_addr_bin : ip_addr_bin_vector ) {
            arp::pktgen_send_garp(tx_bufs[0], iface, ip_addr_bin);
            ++garps_sent;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // If we sample for anything other than 1s, the rate calculation will need to change
        }
    });


    struct rte_mbuf **rx_bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
    rte_pktmbuf_alloc_bulk(mbuf_pool, rx_bufs, burst_size);

    // datafile.open(output_data_filename, std::ios::out | std::ios::binary);
    // if ( (datafile.rdstate() & std::ofstream::failbit ) != 0 ) {
        // fmt::print("WARNING: Unable to open output file \"{}\"\n", output_data_filename);
    // }

    while (true) {
        /* Get burst of RX packets, from first iface of pair. */
        const uint16_t nb_rx = rte_eth_rx_burst(iface, 0, rx_bufs, burst_size);

        num_packets   += nb_rx;
        total_packets += nb_rx;

        for (int i_b = 0; i_b < nb_rx; ++i_b) {
            num_bytes += rx_bufs[i_b]->pkt_len;
            

            bool dump_packet = false;
            if (not RTE_ETH_IS_IPV4_HDR(rx_bufs[i_b]->packet_type)) {
                non_ipv4_packets++;
                dump_packet = true;
                continue;
            }
            ++udp_pkt_counter;

            char* udp_payload = udp::get_udp_payload(rx_bufs[i_b]);
            const detdataformats::DAQEthHeader* daq_hdr = reinterpret_cast<const detdataformats::DAQEthHeader*>(udp_payload);

            // uint64_t unique_str_id = (daq_hdr->det_id<<22) + (daq_hdr->crate_id<<12) + (daq_hdr->slot_id<<8) + daq_hdr->stream_id;
            StreamUID unique_str_id(*daq_hdr);
	    
            // if ((udp_pkt_counter % 1000000) == 0 ) {
            //     std::cout << "\nDAQ HEADER:\n" << *daq_hdr<< "\n";
            // }
            if (stream_stats.find(unique_str_id) == stream_stats.end()) {
                stream_stats[unique_str_id];
                stream_stats[unique_str_id].prev_seq_id = daq_hdr->seq_id - 1;
            }
            stream_stats[unique_str_id].num_bytes += rx_bufs[i_b]->pkt_len;

            if (check_against_previous_stream(daq_hdr, 2048) != 0){
                dump_packet = true;
            }
            if (check_packet_size(rx_bufs[i_b], unique_str_id) != 0){
                dump_packet = true;
            }

            ++stream_stats[unique_str_id].total_packets;
            ++stream_stats[unique_str_id].num_packets;
            stream_stats[unique_str_id].prev_seq_id = daq_hdr->seq_id;

            if (dump_packet && dumped_packet_count < max_packets_to_dump) {
                dumped_packet_count++;

                rte_pktmbuf_dump(stdout, rx_bufs[i_b], rx_bufs[i_b]->pkt_len);
            }
        }

        rte_pktmbuf_free_bulk(rx_bufs, nb_rx);
    }

    return 0;
}                                                              

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum){
    fmt::print("Caught signal {}\n", signum);
    std::exit(signum);
}

int main(int argc, char** argv){
    uint64_t time_per_report = 1;
    uint16_t iface = 0;
    std::vector<std::string> garp_ip_addresses;
    std::vector<std::string> pcie_addresses;

    CLI::App app{"test frame receiver"};
    app.add_option("-g,--garp-ip-address", garp_ip_addresses, "IP Addresses");
    app.add_option("-m,--pcie-mask", pcie_addresses, "PCIE Addresses device mask");
    app.add_option("-s,--exp-frame-size", expected_packet_size, "Expected frame size");
    app.add_option("-i,--iface", iface, "Interface to init");
    app.add_option("-t,--report-interval-time", time_per_report, "Time Per Report");
    app.add_flag("--check-time", check_timestamp, "Report back differences in timestamp");
    app.add_flag("-p,--per-stream-reports", per_stream_reports, "Detailed per stream reports");
    
    CLI11_PARSE(app, argc, argv);

    // Validate arguments
    fmt::print("ips   : {}\n", fmt::join(garp_ip_addresses," | "));
    fmt::print("pcies   : {}\n", fmt::join(pcie_addresses," | "));

    std::regex re_ipv4("[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}");
    std::regex re_pcie("^0{0,4}:[a-fA-F0-9]{2}:[a-fA-F0-9]{2}.[0-9]$");
    
    fmt::print("IP addresses\n");
    bool all_ip_ok = true;
    for( const auto& ip: garp_ip_addresses) {
        bool ip_ok = std::regex_match(ip, re_ipv4);
        fmt::print("- {} {}\n", ip, ip_ok);
        all_ip_ok &= ip_ok;
    }

    fmt::print("PCIE addresses\n");
    bool all_pcie_ok = true;
    for( const auto& pcie: pcie_addresses) {
        bool pcie_ok = std::regex_match(pcie, re_pcie);
        fmt::print("- {} {}\n", pcie, pcie_ok);
        all_pcie_ok &= pcie_ok;
    }

    if (!all_ip_ok or !all_pcie_ok) {
        return -1;
    }


    //    define function to be called when ctrl+c is called.
    std::signal(SIGINT, signal_callback_handler);
    
    std::vector<std::string> eal_args;
    eal_args.push_back("dpdklibds_test_garp");
    for( const auto& pcie: pcie_addresses) {
        eal_args.push_back("-a");
        eal_args.push_back(pcie);
    }


    dunedaq::dpdklibs::ealutils::init_eal(eal_args);

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
    const uint16_t n_rx_qs = 1;
    const uint16_t n_tx_qs = 1;
    const uint16_t rx_ring_size = 2048;
    const uint16_t tx_ring_size = 2048;

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
    ealutils::iface_init(iface, n_rx_qs, n_tx_qs, rx_ring_size, tx_ring_size, mbuf_pools); // just init iface, no TX queues

    lcore_main(mbuf_pools[0].get(), iface, time_per_report, garp_ip_addresses);

    rte_eal_cleanup();

    return 0;
}
