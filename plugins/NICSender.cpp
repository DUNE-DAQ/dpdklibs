/**
 * @file NICSender.cpp NICSender DAQModule implementation
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "dpdklibs/nicreader/Nljs.hpp"

#include "logging/Logging.hpp"
#include "dpdklibs/EALSetup.hpp"

#include "NICSender.hpp"

#include <cinttypes>
#include <chrono>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * @brief Name used by TRACE TLOG calls from this source file
 */
#define TRACE_NAME "NICSender" // NOLINT

/**
 * @brief TRACE debug levels used in this source file
 */
enum
{
  TLVL_ENTER_EXIT_METHODS = 5,
  TLVL_WORK_STEPS = 10,
  TLVL_BOOKKEEPING = 15
};

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif

static const struct rte_eth_conf port_conf_default = {
    .txmode = {
        .offloads = (DEV_TX_OFFLOAD_IPV4_CKSUM |
                     DEV_TX_OFFLOAD_UDP_CKSUM),
},
};

int burst_size = 1;

namespace dunedaq {
namespace dpdklibs {
  using namespace udp;

NICSender::NICSender(const std::string& name)
  : DAQModule(name)
{
  register_command("conf", &NICSender::do_configure);
  register_command("start", &NICSender::do_start);
  register_command("stop", &NICSender::do_stop);
  register_command("scrap", &NICSender::do_scrap);
}

// void *
// pktgen_udp_hdr_ctor(void *hdr, int type)
// {
//     uint16_t tlen;

//     struct rte_ipv4_hdr *ipv4 = hdr;
//     struct rte_udp_hdr *udp   = (struct rte_udp_hdr *)&ipv4[1];

//     struct rte_ether_addr eth_dst_addr = ; /**< Destination Ethernet address */
//     struct rte_ether_addr eth_src_addr = ; /**< Source Ethernet address */

//     uint16_t ether_hdr_size = ; /**< Size of Ethernet header in packet for VLAN ID */
//     uint16_t ipProto = ; /**< TCP or UDP or ICMP */
//     uint16_t pktSize = ;    /**< Size of packet in bytes not counting FCS */

//     uint16_t sport = ;   /**< Source port value */
//     uint16_t dport = ;   /**< Destination port value */



//     /* Create the UDP header */
//     ipv4->src_addr = htonl(pkt->ip_src_addr.addr.ipv4.s_addr);
//     ipv4->dst_addr = htonl(pkt->ip_dst_addr.addr.ipv4.s_addr);

//     ipv4->version_ihl   = (IPv4_VERSION << 4) | (sizeof(struct rte_ipv4_hdr) / 4);
//     tlen                = pkt->pktSize - pkt->ether_hdr_size;
//     ipv4->total_length  = htons(tlen);
//     ipv4->next_proto_id = pkt->ipProto;

//     tlen           = pkt->pktSize - (pkt->ether_hdr_size + sizeof(struct rte_ipv4_hdr));
//     udp->dgram_len = htons(tlen);
//     udp->src_port  = htons(pkt->sport);
//     udp->dst_port  = htons(pkt->dport);

//     if (pkt->dport == VXLAN_PORT_ID) {
//         struct vxlan *vxlan = (struct vxlan *)&udp[1];

//         vxlan->vni_flags = htons(pkt->vni_flags);
//         vxlan->group_id  = htons(pkt->group_id);
//         vxlan->vxlan_id  = htonl(pkt->vxlan_id) << 8;
//     }

//     udp->dgram_cksum = 0;
//     udp->dgram_cksum = rte_ipv4_udptcp_cksum(ipv4, (const void *)udp);
//     if (udp->dgram_cksum == 0)
//         udp->dgram_cksum = 0xFFFF;
// }

void lcore_main(void *arg) {

    uint16_t lid = rte_lcore_id();

    TLOG() << "lid = " << lid;
    if (lid > 2) return;

    TLOG () << "Going to sleep with lid = " << lid;
    rte_delay_us_sleep((lid + 1) * 1000021);
    uint16_t port;

    unsigned nb_ports = rte_eth_dev_count_avail();
    TLOG () << "mbuf with lid = " << lid;
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL") + std::to_string(lid)).c_str(), NUM_MBUFS * nb_ports,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    TLOG () << "mbuf done with lid = " << lid;

    uint16_t portid;

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  // RTE_ETH_FOREACH_DEV(port) {
  //     if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
  //         printf("WARNING, port %u is on remote NUMA node to "
  //                         "polling thread./n/tPerformance will "
  //                         "not be optimal.\n", port);
  //     }

  //     printf("INFO: Port %u has socket id: %u.\n", port, rte_eth_dev_socket_id(port));
  // }

  printf("\n\nCore %u transmitting packets. [Ctrl+C to quit]\n\n", rte_lcore_id());

  /* Run until the application is quit or killed. */
  int burst_number = 0;
  std::atomic<int> num_frames = 0;

  auto stats = std::thread([&]() {
    while (true) {
      // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
      TLOG() << "Rate is " << sizeof(struct ipv4_udp_packet) * num_frames / 1e6 * 8;
      num_frames.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  // std::this_thread::sleep_for(std::chrono::milliseconds(5));
  struct rte_mbuf **pkt = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  if (lid == 2) TLOG() << "Allocation with lid = " << lid;
  rte_pktmbuf_alloc_bulk(mbuf_pool, pkt, burst_size);
  if (lid == 2) TLOG() << "Allocation done with lid = " << lid;

  while (true) {
      port = 0;

      // Message struct
      // struct Message {
      //   // detdataformats::wib::WIBFrame fr;
      //   char ch[16];
      // };

      // Ethernet header
      // struct rte_ether_hdr eth_hdr = {0};
      // eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

      /* Dummy message to transmit */


      //struct rte_mbuf *pkt[burst_size];

      // if (burst_number % 1000 == 0) {
      //   TLOG() << "burst_number =" << burst_number;
      // }

      for (int i = 0; i < burst_size; i++)
      {
        // struct Message msg;

        struct ipv4_udp_packet msg;
        pktgen_packet_ctor(&msg.hdr);

        // msg.fr.set_timestamp(burst_number);
        // msg.fr.set_channel(190, i);
        pkt[i]->pkt_len = sizeof(struct ipv4_udp_packet);
        pkt[i]->data_len = 8000;
        // for (int i = 0; i < 8000 / 11; ++i) {
        //     strcpy(msg.payload + i * 11, "Hello world");
        // }
        // if (port) 
        //     strcpy(msg.payload, first_string.c_str());
        // else
        //     strcpy(msg.payload, second_string.c_str());

        char *ether_mbuf_offset = rte_pktmbuf_mtod_offset(pkt[i], char*, 0);

        rte_memcpy(ether_mbuf_offset, &msg, sizeof(struct ipv4_udp_packet));

        if (false) {
          rte_pktmbuf_dump(stdout, pkt[i], pkt[i]->pkt_len);
        }
      }
      burst_number++;

      /* Send burst of TX packets. */
      int sent = 0;
      uint16_t nb_tx;
      while(sent < burst_size)
      {
            nb_tx = rte_eth_tx_burst(port, lid-1, pkt, burst_size - sent);
        sent += nb_tx;
        num_frames += nb_tx;
      }

      /* Free any unsent packets. */
      if (unlikely(nb_tx < burst_size))
      {
        uint16_t buf;
        for (buf = nb_tx; buf < burst_size; buf++)
        {
          rte_pktmbuf_free(pkt[buf]);
        }
      }
      rte_eth_tx_done_cleanup(port, lid-1, 0);
  }
}

NICSender::~NICSender()
{
}

void
NICSender::init(const data_t& args)
{
}

void
NICSender::dpdk_configure()
{
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    // Init EAL
    int argc = 0;
    std::vector<char*> v{"test"};
    ealutils::init_eal(argc, v.data());
    // if (ret < 0) {
    //     rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
    // }
    // TLOG() << "EAL INIT WORKED";

    // Check that there is an even number of ports to send/receive on
    // nb_ports = rte_eth_dev_count_avail();
    // TLOG() << "There are " << nb_ports << " ports available";
    // if (nb_ports < 2 || (nb_ports & 1)) {
    //     TLOG() << "There are " << nb_ports << " ports available";
    //     TLOG() << "There are " << rte_eth_dev_count_total() << " ports in total";
    //     rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
    // }

    // printf("RTE_MBUF_DEFAULT_BUF_SIZE = %d\n", RTE_MBUF_DEFAULT_BUF_SIZE);

    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 0, tx_rings = 2;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;
    uint16_t port = 0;

    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    // if (retval != 0)
    //     return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    // if (retval != 0)
    //   return retval;

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
      retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
      // if (retval < 0)
      //   return retval;
    }

    retval = rte_eth_dev_start(port);
    // if (retval < 0)
    //   return retval;

    /* Display the port MAC address. */
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    // if (retval != 0)
    //   return retval;

    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
           port,
           addr.addr_bytes[0],
           addr.addr_bytes[1],
           addr.addr_bytes[2],
           addr.addr_bytes[3],
           addr.addr_bytes[4],
           addr.addr_bytes[5]);

    rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
    
    uint16_t mtu;
    rte_eth_dev_get_mtu(port, &mtu);
    TLOG() << "MTU = " << mtu;
}

void
NICSender::do_configure(const data_t& args)
{

    module_conf_t cfg = args.get<module_conf_t>();

    m_burst_size = cfg.burst_size;
    m_number_of_cores = cfg.number_of_cores;
    m_rate = cfg.rate;

    dpdk_configure();

}

void
NICSender::do_start(const data_t& args)
{
  m_run_mark.store(true);

  rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, NULL, SKIP_MAIN);

}

void
NICSender::do_stop(const data_t& args)
{
  m_run_mark.store(false);
}

void
NICSender::do_scrap(const data_t& args)
{
}

void
NICSender::get_info(opmonlib::InfoCollector& ci, int level)
{

}

} // namespace dpdklibs
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dpdklibs::NICSender)
