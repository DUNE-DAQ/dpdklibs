
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

    std::string conf_filepath = "";

    std::ostream & operator <<(std::ostream &out, const StreamUID &obj) {
        return out << static_cast<std::string>(obj);
    }

    std::map<StreamUID, std::string> src_list;

    std::atomic<bool> stop_app = false;
    // Apparently only 8 and above works for "burst_size"

    // From the dpdk documentation, describing the rte_eth_rx_burst
    // function (and keeping in mind that their "nb_pkts" variable is the
    // same as our "burst size" variable below):
    // "Some drivers using vector instructions require that nb_pkts is
    // divisible by 4 or 8, depending on the driver implementation."

    constexpr int burst_size = 256;

    int q_per_lcore = 1;
    json output_json;

    int cur_queue = 0;
    int cur_lcore = 1;
    int q_in_core = 0;

    int time_to_run = 2;


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

    struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
    rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);



    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    while ((std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - begin).count()) < time_to_run) {
        /* Get burst of RX packets, from first iface of pair. */
        const uint16_t nb_rx = rte_eth_rx_burst(iface, 0, bufs, burst_size);

        for (int i_b = 0; i_b < nb_rx; ++i_b) {
            if (not RTE_ETH_IS_IPV4_HDR(bufs[i_b]->packet_type)) {
                continue;
            }

            char* udp_payload = dunedaq::dpdklibs::udp::get_udp_payload(bufs[i_b]);
            const dunedaq::detdataformats::DAQEthHeader* daq_hdr = reinterpret_cast<const dunedaq::detdataformats::DAQEthHeader*>(udp_payload);
            if (daq_hdr == nullptr){
                fmt::print("IT IS A NULL PTR\n");
            }
            StreamUID unique_str_id = {daq_hdr->det_id, daq_hdr->crate_id, daq_hdr->slot_id, daq_hdr->stream_id};

            if (src_list.find(unique_str_id) == src_list.end()) {
                struct dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr * pkt = rte_pktmbuf_mtod(bufs[i_b], struct dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr *);
                std::string src_ip = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(
                    dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.src_addr))
                );
                fmt::print("{}\n", src_ip);
                bool found = false;
                for (auto it = src_list.begin(); it != src_list.end(); ++it){
                    if (it->second == src_ip){
                        found = true;
                        break;
                    }
                }
                src_list[unique_str_id] = src_ip;
                if (not found){
                    output_json["0"][std::to_string(cur_lcore)][std::to_string(cur_queue)] = src_list[unique_str_id];
                    ++cur_queue;
                    ++q_in_core;
                    if (q_in_core >= q_per_lcore){
                        ++cur_lcore;
                        q_in_core = 0;
                    }
                }
            }
            rte_pktmbuf_free_bulk(bufs, nb_rx);
        }
    }
    fmt::print("App stopped\n");

    return 0;
}

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum){
    fmt::print("Caught signal {}\n", signum);
    // Terminate program
    std::exit(signum);
}

int main(int argc, char** argv){
    uint64_t time_per_report = 1;
    uint16_t iface = 0;

    CLI::App app{"test frame receiver"};
    app.add_option("-q", q_per_lcore, "Queues per Lcore");
    app.add_option("-t", time_to_run, "Time to run");
    app.add_option("-c,conf", conf_filepath, "configuration Json file path");
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
        mbuf_pools[i] = dunedaq::dpdklibs::ealutils::get_mempool(ss.str());
        bufs[i] = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
        rte_pktmbuf_alloc_bulk(mbuf_pools[i].get(), bufs[i], burst_size);
    }

    // Setting up only one iface
    fmt::print("Initialize only iface {}!\n", iface);
    dunedaq::dpdklibs::ealutils::iface_init(iface, n_rx_qs, 0, mbuf_pools); // just init iface, no TX queues

    lcore_main(mbuf_pools[0].get(), iface, time_per_report);

    std::ofstream o(conf_filepath);
    o << std::setw(4) << output_json << std::endl;
    fmt::print("{}\n",output_json.dump(4));

    rte_eal_cleanup();
    fmt::print("\n======== APP SUCCESSFULLY COMPLETE. EXPECT ERROR ========\n\n");
    return 0;
}
