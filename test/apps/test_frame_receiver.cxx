
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
int dumped_packet_count = 0;

// uint64_t prev_timestamp_of_stream[NSTREAM] = { 0 };
std::array<std::atomic<uint64_t>, NSTREAM> prev_timestamp_of_stream = {0};
// bool prev_stream_exists = false;

std::atomic<int> num_packets = 0;
std::atomic<int> num_bytes = 0;
std::atomic<uint64_t> total_packets = 0;
std::atomic<uint64_t> non_ipv4_packets = 0;
std::atomic<uint64_t> failed_packets = 0;

std::ofstream datafile;
const std::string output_data_filename = "dpdklibs_test_frame_receiver.dat";
} // namespace

static const struct rte_eth_conf port_conf_default = { .rxmode = {
                                                         .mtu = 9000,
                                                         .offloads = (DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM),
                                                       } };

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

  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n", port, addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2], addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  retval = rte_eth_promiscuous_enable(port);
  if (retval != 0)
    return retval;

  return 0;
}

static inline int
check_packet(const struct rte_mbuf& packet, int expected_size, uint32_t expected_type)
{
  if (packet.nb_segs > 1) {
    TLOG(TLVL_WARNING) << "It appears a packet is spread across more than one receiving "
                          "buffer; there's currently no logic in this program to handle this";
    return 1;
  }

  if (packet.packet_type != expected_type) {
    TLOG(TLVL_WARNING) << fmt::format("Found packet type 0x{:x}, expected 0x{:x} ", packet.packet_type, expected_type);
    return 2;
  }

  if (packet.pkt_len != expected_size) {
    TLOG(TLVL_WARNING) << fmt::format("Found packet of size {}, expected {} ", packet.pkt_len, expected_size);
    return 3;
  }

  return 0;
}

static inline int
check_against_previous_stream(const detdataformats::DAQEthHeader* daq_header, uint64_t exp_ts_diff)
{
  // TLOG() << "\nIN check against previous stream\n";
  uint64_t stream_id = daq_header->stream_id;
  uint64_t stream_ts = daq_header->timestamp;
  int ret_val = 0;
  if ( prev_timestamp_of_stream[stream_id] == 0 ) {
    prev_timestamp_of_stream[stream_id] = stream_ts;
    return ret_val;
  }

//   if (prev_stream_exists) {
  TLOG() << fmt::format("0x{:016x} 0x{:016x}", prev_timestamp_of_stream[stream_id], stream_ts);
  // TLOG() << "current timestamp of stream: " << stream_ts;
  uint64_t ts_difference = stream_ts - prev_timestamp_of_stream[stream_id];
  if (ts_difference != exp_ts_diff) {
      TLOG() << fmt::format("WARNING: wrong timestamp difference {} for stream {} expected ({}) 0x{:016x} 0x{:016x}", ts_difference, stream_id, exp_ts_diff, stream_ts,prev_timestamp_of_stream[stream_id]);
      ret_val = 1;
  }

   
//   else {
//     prev_stream_exists = true;
//   }
  // std::cout << "\n";
  // TLOG() << "prev timestamp of stream before: " << prev_timestamp_of_stream[stream_id];
  // TLOG() << "current timestamp of stream: " << stream_ts;
  prev_timestamp_of_stream[stream_id] = stream_ts;
  // TLOG() << "prev timestamp of stream after: " << prev_timestamp_of_stream[stream_id];
  // std::cout << "\n";
  return ret_val;
}

static int
lcore_main(struct rte_mempool* mbuf_pool)
{
  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */

  uint16_t port = std::numeric_limits<uint16_t>::max();

  RTE_ETH_FOREACH_DEV(port)
  if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != static_cast<int>(rte_socket_id())) {
    TLOG(TLVL_WARNING) << "WARNING, port " << port
                       << " is on remote NUMA node to polling "
                          "thread.\n\tPerformance will not be optimal.\n";
  }

  auto stats = std::thread([&]() {
    while (true) {
      TLOG() << fmt::format("Packets/s: {} Bytes/s: {} Total packets: {} Non-IPV4 packets: {} "
                            "Failed packets: {}",
                            num_packets,
                            num_bytes,
                            total_packets,
                            non_ipv4_packets,
                            failed_packets);
      num_packets.exchange(0);
      num_bytes.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1)); // If we sample for anything other than 1s,
                                                            // the rate calculation will need to change
    }
  });

  struct rte_mbuf** bufs = (rte_mbuf**)malloc(sizeof(struct rte_mbuf*) * burst_size);
  rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, burst_size);

  datafile.open(output_data_filename, std::ios::out | std::ios::binary);
  if ((datafile.rdstate() & std::ofstream::failbit) != 0) {
    TLOG(TLVL_WARNING) << "Unable to open output file \"" << output_data_filename << "\"";
  }

  uint64_t udp_pkt_counter = 0;
  while (true) {
    RTE_ETH_FOREACH_DEV(port)
    {
      /* Get burst of RX packets, from first port of pair. */
      const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, burst_size);

      num_packets += nb_rx;
      total_packets += nb_rx;

      for (int i_b = 0; i_b < nb_rx; ++i_b) {
        // TLOG() << "\nin loop\n";
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
          TLOG() << *daq_header;
        } 

        if (check_against_previous_stream(daq_header, 2048) != 0){
          TLOG() << udp_pkt_counter;
          TLOG() <<  *daq_header;
        	dump_packet = true;
        	failed_packets++;
        }


        // if (check_packet(*bufs[i_b], expected_packet_size,
        // expected_packet_type) != 0) { 	dump_packet = true; 	failed_packets++;
        // }
        // else {
        // 	if (check_against_previous_stream(daq_header) != 0){
        // 	dump_packet = true;
        // 	failed_packets++;
        // 	}
        // }

        if (dump_packet && dumped_packet_count < max_packets_to_dump) {
          dumped_packet_count++;

          rte_pktmbuf_dump(stdout, bufs[i_b], bufs[i_b]->pkt_len);

          // TLOG() << *daq_header;

          // datafile.write(reinterpret_cast<const char*>(bufs[i_b]),
          // bufs[i_b]->pkt_len);
        }
      }

      rte_pktmbuf_free_bulk(bufs, nb_rx);
    }
  }
  return 0;
}

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void
signal_callback_handler(int signum)
{
  TLOG() << "Caught signal " << signum;
  if (datafile.is_open()) {
    datafile.close();
  }
  // Terminate program
  std::exit(signum);
}

int
main(int argc, char** argv)
{
  //	define function to be called when ctrl+c is called.
  std::signal(SIGINT, signal_callback_handler);
  // initialise eal with argc and argv
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }

  auto nb_ports = rte_eth_dev_count_avail();
  TLOG() << "# of available ports: " << nb_ports;

  // // Check that there is an even number of ports to send/receive on
  // if (nb_ports < 2 || (nb_ports & 1)) {
  //	rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
  //}

  struct rte_mempool* mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, default_mbuf_size, rte_socket_id());

  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "ERROR: rte_pktmbuf_pool_create returned null");
  }

  // Initialize all ports
  uint16_t portid = std::numeric_limits<uint16_t>::max();
  RTE_ETH_FOREACH_DEV(portid)
  {
    int retval = port_init(portid, mbuf_pool);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %" PRIu16 "; port_init returned (%d)\n", portid, retval);
    }
  }

  lcore_main(mbuf_pool);

  rte_eal_cleanup();

  return 0;
}
