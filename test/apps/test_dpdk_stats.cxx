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

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

namespace {
  constexpr int burst_size = 256;

  std::atomic<uint64_t> num_packets = 0;
  std::atomic<uint64_t> num_bytes = 0;
  std::atomic<uint64_t> num_errors = 0;
  std::atomic<uint64_t> num_missed = 0; 

} // namespace ""

static int
lcore_main(struct rte_mempool *mbuf_pool)
{
  uint16_t iface = 0;
  TLOG() << "Launch lcore for interface: " << iface;

  struct rte_mbuf **tx_bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, burst_size);

  // Reset internal ETH DEV stat counters.
  rte_eth_stats_reset(iface);
  rte_eth_xstats_reset(iface);

//////////// RS FIXME -> Copy pasta DPDK Docs, of course the docs are super misleading....
    struct rte_eth_xstat_name *xstats_names;
    uint64_t *xstats_ids;
    uint64_t *values;
    int len, i;

    // Get number of stats
    len = rte_eth_xstats_get_names_by_id(iface, NULL, NULL, 0);
    if (len < 0) {
        printf("Cannot get xstats count\n");
    }

    // Get names of HW registered stat fields
    xstats_names = (rte_eth_xstat_name*)(malloc(sizeof(struct rte_eth_xstat_name) * len));
    if (xstats_names == NULL) {
        printf("Cannot allocate memory for xstat names\n");
    }

    // Retrieve xstats names, passing NULL for IDs to return all statistics
    if (len != rte_eth_xstats_get_names(iface, xstats_names, len)) {
        printf("Cannot get xstat names\n");
    }

    // Allocate value fields
    values = (uint64_t*)(malloc(sizeof(values) * len));
    if (values == NULL) {
        printf("Cannot allocate memory for xstats\n");
    }

    // Getting xstats values (this is that we call in a loop/get_info
    if (len != rte_eth_xstats_get_by_id(iface, NULL, values, len)) {
        printf("Cannot get xstat values\n");
    }

    // Print all xstats names and values to be amazed (WOW!)
    for (i = 0; i < len; i++) {
      TLOG() << "Name: " << xstats_names[i].name << " value: " << values[i];
    }

/////////////// RS FIXME: Stats thread spawn. Passed with the scope of attrocities above...
  auto stats = std::thread([&]() {

///////////// RS FIXME: Simple PMD based stats monitoring is also possible
    struct rte_eth_stats iface_stats;
    while (true) {
      // RS: poll out dev stats. (SIMPLE MODE)
      rte_eth_stats_get(iface, &iface_stats);
      num_packets = (uint64_t)iface_stats.ipackets;
      num_bytes = (uint64_t)iface_stats.ibytes;
      num_missed = (uint64_t)iface_stats.imissed;
      num_errors = (uint64_t)iface_stats.ierrors;
      TLOG() << " Total packets: " << num_packets
             << " Total bytes: " << num_bytes
             << " Total missed: " << num_missed
             << " Total errors: " << num_errors;
      // Queue based counters doesn't seem to work neither here neither in module... :((((((
      for( unsigned long i = 0; i < RTE_ETHDEV_QUEUE_STAT_CNTRS; i++ ){
        TLOG() << "HW iface queue[" << i << "] received: " << (uint64_t)iface_stats.q_ipackets[i];
      }

////////////// RS FIXME: HW counter based stats monitoring. Fields initialized just before thread spawn.
      if (len != rte_eth_xstats_get_by_id(iface, NULL, values, len)) {
        TLOG() << "Cannot get xstat values!";
      } else {
        for (i = 0; i < len; i++) {
          TLOG() << "Name: " << xstats_names[i].name << " value: " << values[i];
        }
      }
      int reset_res = rte_eth_xstats_reset(iface);
      TLOG() << "Reset notification result: " << reset_res; 
////////////// RS FIXME: HW counter based loop ends.

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
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
  }

  // Iface ID and its queue numbers
  int iface_id = 0;
  const uint16_t rx_qs = 5;
  const uint16_t tx_qs = 1;
  const uint16_t rx_ring_size = 1024;
  const uint16_t tx_ring_size = 1024;
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
  ealutils::iface_init(iface_id, rx_qs, tx_qs, rx_ring_size, tx_ring_size, mbuf_pools, false, false);
  ealutils::iface_promiscuous_mode(iface_id, false); // should come from config

  // Launch lcores
  lcore_main(mbuf_pools[0].get());

  // Cleanup
  TLOG() << "EAL cleanup...";
  ealutils::wait_for_lcores();
  rte_eal_cleanup();
  
  return 0;
}
