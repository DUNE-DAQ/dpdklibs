/* Application will run until quit or killed. */

#include "dpdklibs/EALSetup.hpp"
#include "logging/Logging.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/arp/ARP.hpp"
#include "dpdklibs/ipv4_addr.hpp"

#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <sstream>
#include <stdint.h>
#include <limits>
#include <iomanip>
#include <fstream>
#include <csignal>


#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <regex>

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

namespace {
  constexpr int burst_size = 256;

  std::atomic<int> num_packets = 0;
  std::atomic<int> num_bytes = 0;
  std::atomic<int64_t> total_packets = 0;
  std::atomic<int64_t> failed_packets = 0;

  std::atomic<int64_t> garps_sent = 0;

} // namespace ""

static int
lcore_main(struct rte_mempool *mbuf_pool, const std::vector<std::string>& ip_addr_strs)
{
  uint16_t iface = 0;
  TLOG() << "Launch lcore for interface: " << iface;

  // IP for ARP
  // std::vector<std::string> ip_addr_strs = {
  //   "10.73.139.26",
  //   "10.73.139.27"
  // };

  std::vector<rte_be32_t> ip_addr_bin_vector;
  for( const auto& ip_addr_str : ip_addr_strs ) {
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
      TLOG() << "Packets/s: " << num_packets << " Bytes/s: " << num_bytes << " Total packets: " << total_packets << " Failed packets: " << failed_packets;
      num_packets.exchange(0);
      num_bytes.exchange(0);

      for(const auto& ip_addr_bin : ip_addr_bin_vector ) {
        arp::pktgen_send_garp(tx_bufs[0], iface, ip_addr_bin);
        ++garps_sent;
      }

      std::this_thread::sleep_for(std::chrono::seconds(1)); // If we sample for anything other than 1s, the rate calculation will need to change
    }
  });

  struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

  bool once = true; // one shot variable
  while (true) {
    const uint16_t nb_rx = rte_eth_rx_burst(iface, 0, bufs, burst_size);
    if (nb_rx != 0) {
      num_packets += nb_rx;
      // Iterate on burst packets
      for (int i_b=0; i_b<nb_rx; ++i_b) {
        num_bytes += bufs[i_b]->pkt_len;

        // Check for segmentation
        if (bufs[i_b]->nb_segs > 1) {
            TLOG() << "It appears a packet is spread across more than one receiving buffer;"
                   << " there's currently no logic in this program to handle this";
        }

        // Check packet type
        auto pkt_type = bufs[i_b]->packet_type;
        //// Handle non IPV4 packets
        if (not RTE_ETH_IS_IPV4_HDR(pkt_type)) {
          TLOG() << "Non-Ethernet packet type: " << (unsigned)pkt_type;
          if (pkt_type == RTE_PTYPE_L2_ETHER_ARP) {
            TLOG() << "TODO: Handle ARP request!";
            rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);
            //arp::pktgen_process_arp(bufs[i_b], 0, ip_addr_bin);
          } else if (pkt_type == RTE_PTYPE_L2_ETHER_LLDP) {
            TLOG() << "TODO: Handle LLDP packet!";
            rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);
          } else {
            TLOG() << "Unidentified! Dumping...";
            rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);
          }
          continue;
        }
      }
      rte_pktmbuf_free_bulk(bufs, nb_rx);
    }
  } // main loop


  return 0;
}

int
main(int argc, char* argv[])
{  

  std::vector<std::string> ip_addresses;
  std::vector<std::string> pcie_addresses;

  CLI::App app{"test_garp"};
  app.add_option("-i", ip_addresses, "IP Addresses");
  app.add_option("-p", pcie_addresses, "PCIE Addresses");
  CLI11_PARSE(app, argc, argv);

  fmt::print("ips   : {}\n", fmt::join(ip_addresses," | "));
  fmt::print("pcies   : {}\n", fmt::join(pcie_addresses," | "));

  std::regex re_ipv4("[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}");
  std::regex re_pcie("^0{0,4}:[a-fA-F0-9]{2}:[a-fA-F0-9]{2}.[0-9]$");
  
  fmt::print("IP addresses\n");
  bool all_ip_ok = true;
  for( const auto& ip: ip_addresses) {
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

  std::vector<std::string> eal_args;
  eal_args.push_back("dpdklibds_test_garp");
  for( const auto& pcie: pcie_addresses) {
    eal_args.push_back("-a");
    eal_args.push_back(pcie);
  }


  dunedaq::dpdklibs::ealutils::init_eal(eal_args);

  // Iface ID and its queue numbers
  int iface_id = 0;
  const uint16_t rx_qs = 1;
  const uint16_t tx_qs = 1;
  const uint16_t rx_ring_size = 2048;
  const uint16_t tx_ring_size = 2048;
  // Get pool
  std::map<int, std::unique_ptr<rte_mempool>> mbuf_pools;
  TLOG() << "Allocating pool";
  for (unsigned p_i = 0; p_i<rx_qs; ++p_i) {
    std::ostringstream ss;
    ss << "MBP-" << p_i;
    mbuf_pools[p_i] = ealutils::get_mempool(ss.str());
  }

  // Setup interface
  auto nb_ifaces = rte_eth_dev_count_avail();
  TLOG() << "# of available interfaces: " << nb_ifaces;
  TLOG() << "Initialize interface " << iface_id;
  ealutils::iface_init(iface_id, rx_qs, tx_qs, rx_ring_size, tx_ring_size, mbuf_pools);
  ealutils::iface_promiscuous_mode(iface_id, true); // should come from config

  // Launch lcores
  lcore_main(mbuf_pools[0].get(), ip_addresses);

  // Cleanup
  TLOG() << "EAL cleanup...";
  ealutils::wait_for_lcores();
  rte_eal_cleanup();
  
  return 0;
}
