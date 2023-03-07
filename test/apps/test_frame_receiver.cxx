#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <sstream>
#include <stdint.h>
#include <limits>
#include <iostream>
#include <iomanip>

#include "logging/Logging.hpp"
#include "detdataformats/wibeth/WIBEthFrame.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

// Apparently only 8 and above works for "burst_size"

// From the dpdk documentation, describing the rte_eth_rx_burst
// function (and keeping in mind that their "nb_pkts" variable is the
// same as our "burst size" variable below):
// "Some drivers using vector instructions require that nb_pkts is
// divisible by 4 or 8, depending on the driver implementation."

constexpr int burst_size = 256;
constexpr int expected_packet_size = 7188;
constexpr int default_mbuf_size = 9000;  // As opposed to RTE_MBUF_DEFAULT_BUF_SIZE
constexpr bool is_debug = true;

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

namespace {

  // Remember that the trace levels for TLOG_DEBUG messages are offset by 8
  // E.g., to see time diffs on your screen you'd want to run "tonS 40 -n test_frame_receiver"
  // See https://dune-daq-sw.readthedocs.io/en/latest/packages/logging for more
  
  constexpr int trace_show_time_diffs = 32;
  constexpr int trace_show_all_channel_values = 33;
  constexpr int trace_show_rx_packet_count = 34;
} // namespace ""

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = 9000,
        .offloads = (DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM),
        }
};

static inline int
port_init(uint16_t port, struct rte_mempool* mbuf_pool)
{
  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = 1, tx_rings = 0;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf txconf;

  if (!rte_eth_dev_is_valid_port(port))
    return -1;

  retval = rte_eth_dev_info_get(port, &dev_info);
  if (retval != 0) {
    printf("Error during getting device (port %u) info: %s\n", port, strerror(-retval));
    return retval;
  }

  if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
    port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0)
    return retval;

    rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
  { /* scope */
    uint16_t mtu;
    rte_eth_dev_get_mtu(port, &mtu);
    TLOG() << "Port: " << port << " MTU: " << mtu;
  }

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0)
    return retval;

  /* Allocate and set up 1 RX queue per Ethernet port. */
  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
      return retval;
  }

  txconf = dev_info.default_txconf;
  txconf.offloads = port_conf.txmode.offloads;
  /* Allocate and set up 1 TX queue per Ethernet port. */
  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0)
      return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if (retval < 0)
    return retval;

  /* Display the port MAC address. */
  struct rte_ether_addr addr;
  retval = rte_eth_macaddr_get(port, &addr);
  if (retval != 0)
    return retval;

  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
         port,
         addr.addr_bytes[0],
         addr.addr_bytes[1],
         addr.addr_bytes[2],
         addr.addr_bytes[3],
         addr.addr_bytes[4],
         addr.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  retval = rte_eth_promiscuous_enable(port);
  if (retval != 0)
    return retval;

  return 0;
}


static std::ostream&
operator<<(std::ostream& o, const detdataformats::wibeth::WIBEthFrame& fr) {
  o << "DAQEthHeader contents:\n" << fr.daq_header << "\n";
  o << "======================================================================";

  // o << "The " << detdataformats::wibeth::WIBEthFrame::s_num_adc_words_per_ts << " ADC values from first time sample in the frame:\n";
  // for (int i = 0; i < detdataformats::wibeth::WIBEthFrame::s_num_adc_words_per_ts; ++i) {
  //   o << fr.adc_words[i][0] << " ";
  // }

  return o;
}


static int
lcore_main(struct rte_mempool *mbuf_pool)
{
  uint16_t port = std::numeric_limits<uint16_t>::max();

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  RTE_ETH_FOREACH_DEV(port)
    if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != static_cast<int>(rte_socket_id())) {
      TLOG(TLVL_WARNING) << "WARNING, port " << port << " is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n";
    } else {
      TLOG() << "rte_eth_dev_socket_id(" << port << ") == " << rte_eth_dev_socket_id(port) << ", rte_socket_id() == " << static_cast<int>(rte_socket_id());
    }

  /* Run until the application is quit or killed. */
  int burst_number = 0;
  std::atomic<int> num_frames = 0;
  std::atomic<int> num_bytes = 0;

  auto stats = std::thread([&]() {
    while (true) {
      TLOG() << "Frames/s rate is " << num_frames;
      TLOG() << "Bytes/s rate is " << num_bytes;
      num_frames.exchange(0);
      num_bytes.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  struct rte_mbuf **bufs = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

  while (true) {

    RTE_ETH_FOREACH_DEV(port)
    {

      /* Get burst of RX packets, from first port of pair. */
      const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, burst_size);

      if (nb_rx == burst_size) {
	TLOG() << "Got maximum number of packets (" << burst_size << ") back from call to rte_eth_rx_burst on port " << port;
      }

      num_frames += nb_rx;

      for (int i_b = 0; i_b < nb_rx; ++i_b) {

	if (bufs[i_b]->nb_segs > 1) {
	  TLOG(TLVL_WARNING) << "It appears a packet is spread across more than one receiving buffer; there's currently no logic in this program to handle this";
	}
	
	num_bytes += bufs[i_b]->pkt_len;

	if (is_debug && bufs[i_b]->pkt_len == expected_packet_size) {
	  rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);

	  auto fr = rte_pktmbuf_mtod_offset(bufs[i_b], detdataformats::wibeth::WIBEthFrame*, sizeof(struct rte_ether_hdr));
	  
	  std::stringstream framestream;
	  framestream << *fr;
	  TLOG() << framestream.str().c_str();
	}
      }

      rte_pktmbuf_free_bulk(bufs, nb_rx);
    }
  }
  return 0;
}

  
int
main(int argc, char* argv[])
{
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    // Init EAL
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
    }

    // Check that there is an even number of ports to send/receive on
    nb_ports = rte_eth_dev_count_avail();
    printf("Available ports: %d\n", nb_ports);
    //if (nb_ports < 2 || (nb_ports & 1)) {
    //    rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
    //}

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
        MBUF_CACHE_SIZE, 0, default_mbuf_size, rte_socket_id());

    if (mbuf_pool == NULL) {
        rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    }

    // Initialize all ports
    RTE_ETH_FOREACH_DEV(portid) {
        if (port_init(portid, mbuf_pool) != 0) {
            rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
        }
    }

    // Call lcore_main on the main core only
    // for (int i=0; i < 2; ++i) {
    //   rte_eal_remote_launch(lcore_main, mbuf_pool, i);
    // }
    lcore_main(mbuf_pool);

    // clean up the EAL
    rte_eal_cleanup();

    return 0;
}
