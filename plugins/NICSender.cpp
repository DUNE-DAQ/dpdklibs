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

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "dpdklibs/udp/PacketCtor.hpp"
#include "detdataformats/tde/TDE16Frame.hpp"

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

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

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

void
eth_hdr_ctor(struct rte_ether_hdr *eth_hdr){

  char mac_address[] = "ec:0d:9a:8e:b9:88";
  //char router_mac_address[] = "00:25:90:ed:d5:70"; // farm 21

  rte_ether_addr addr;
  addr.addr_bytes[0] = (int)('e') >> 4 | (int)('c');
  addr.addr_bytes[1] = (int)('0') >> 4 | (int)('d');
  addr.addr_bytes[2] = (int)('9') >> 4 | (int)('a');
  addr.addr_bytes[3] = (int)('8') >> 4 | (int)('e');
  addr.addr_bytes[4] = (int)('b') >> 4 | (int)('9');
  addr.addr_bytes[5] = (int)('8') >> 4 | (int)('8');
  // get_ether_addr6(mac_address, &addr);

  eth_hdr->dst_addr = addr;

  uint8_t port_id = 0;
  rte_eth_macaddr_get(port_id, &eth_hdr->src_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

  // eth_hdr->src_addr = {10, 73, 139, 16};
  // eth_hdr->ether_type = ;
}

void
pktgen_udp_hdr_ctor(rte_ipv4_hdr *ipv4, rte_udp_hdr *udp)
{
    uint16_t tlen;

    // struct rte_ipv4_hdr *ipv4 = reinterpret_cast<struct rte_ipv4_hdr *>(hdr);
    // struct rte_udp_hdr *udp   = (struct rte_udp_hdr *)&ipv4[1];

    struct rte_ether_addr eth_dst_addr = {10, 73, 139, 17}; /**< Destination Ethernet address */
    // struct rte_ether_addr eth_src_addr = "10.73.139.16"; /**< Source Ethernet address */

    // uint16_t ether_hdr_size = ; /**< Size of Ethernet header in packet for VLAN ID */
    // uint16_t ipProto = ; /**< TCP or UDP or ICMP */
    // uint16_t pktSize = ;    /**< Size of packet in bytes not counting FCS */

    uint16_t sport = 0;   /**< Source port value */
    uint16_t dport = 0;   /**< Destination port value */

    /* Create the UDP header */
    // ipv4->src_addr = htonl(pkt->ip_src_addr.addr.ipv4.s_addr);
    // ipv4->dst_addr = eth_dst_addr;

    // ipv4->version_ihl   = (IPv4_VERSION << 4) | (sizeof(struct rte_ipv4_hdr) / 4);
    // tlen                = pkt->pktSize - pkt->ether_hdr_size;
    // ipv4->total_length  = htons(tlen);
    // ipv4->next_proto_id = pkt->ipProto;

    // tlen           = pkt->pktSize - (pkt->ether_hdr_size + sizeof(struct rte_ipv4_hdr));
    // udp->dgram_len = htons(tlen);
    udp->src_port  = htons(sport);
    udp->dst_port  = htons(dport);

    // if (pkt->dport == VXLAN_PORT_ID) {
    //     struct vxlan *vxlan = (struct vxlan *)&udp[1];

    //     vxlan->vni_flags = htons(pkt->vni_flags);
    //     vxlan->group_id  = htons(pkt->group_id);
    //     vxlan->vxlan_id  = htonl(pkt->vxlan_id) << 8;
    // }

    udp->dgram_cksum = 0;
    udp->dgram_cksum = rte_ipv4_udptcp_cksum(ipv4, (const void *)udp);
    if (udp->dgram_cksum == 0)
        udp->dgram_cksum = 0xFFFF;
}

struct lcore_args
{
  NICSender *ns;
};

template<typename T>
int lcore_main(void *arg)
{
  lcore_args *largs = static_cast<lcore_args*>(arg);
  uint16_t lid = rte_lcore_id();

  std::vector<uint32_t> ips;
  for (auto& [key, val]: largs->ns->m_core_map32) {
    if (key == lid) { ips = val; TLOG() << "ips has now size " << ips.size();}
    for (auto& elem : val)
      TLOG() << "Core map val: " << key << " " << elem;
  }

  // TODO: Check why it crashes without this line
  int m_burst_size = 1;

  TLOG() << "lid = " << lid;

  TLOG () << "Going to sleep with lid = " << lid;
  rte_delay_us_sleep((lid + 1) * 1000021);
  uint16_t port;

  unsigned nb_ports = rte_eth_dev_count_avail();
  TLOG () << "mbuf with lid = " << lid;
  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL") + std::to_string(lid)).c_str(), NUM_MBUFS * nb_ports,
      MBUF_CACHE_SIZE, 0, 9800, rte_socket_id());
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

  int burst_number = 0;
  std::atomic<int> num_frames = 0;

  auto stats = std::thread([&]() {
    while (true) {
      // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
      TLOG() << "Rate is " << (sizeof(T)+42) * num_frames / 1e6 * 8;
      num_frames.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  TLOG() << "Doing malloc";
  struct rte_mbuf **pkt = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * m_burst_size);
  rte_pktmbuf_alloc_bulk(mbuf_pool, pkt, m_burst_size);
  TLOG() << "Malloc done";

  size_t i = 0;
  TLOG() << "Got ips with lid " << lid;
  while (true) {
    // TLOG() << "Inside the loop";
    i++;
    // TLOG() << "Index is " << i % ips.size();
    uint8_t ip = ips[i % ips.size()] & 0xff;
    // TODO: Why does it crash when accessing class members?
    // TLOG() << m_run_mark.load();
    port = 0;

    // Ethernet header
    // struct rte_ether_hdr eth_hdr = {0};
    // eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    for (int i = 0; i < m_burst_size; i++)
    {

      T msg;
      msg.set_timestamp(0);
      // pktgen_packet_ctor(&msg.hdr);

      // struct rte_ether_hdr eth_hdr;
      // eth_hdr_ctor(&eth_hdr);

      // rte_ipv4_hdr hdr;
      // rte_udp_hdr hdr_udp;
      // pktgen_udp_hdr_ctor(&hdr, &hdr_udp);
      // pktgen_udp_hdr_ctor(&hdr);

      // pkt[i]->pkt_len = sizeof(rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(struct ipv4_udp_packet);
      // pkt[i]->pkt_len = sizeof(T)+42;
      // TLOG() << "pkt_len = " << sizeof(T) + 42; 9014 for TDE + 42
      pkt[i]->data_len = 8996;

      uint8_t ary[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEC, 0x0D, 0x9A, 0x8E, 0xBA, 0x10, 0x08, 0x00, 0x45, 0x00, 0x23, 0x16, 0xFB, 0xD7, 0x00, 0x00, 0x05, 0x11, 0x6C, 0x4C, 0x0A, 0x49, 0x8B, ip, 0x0A, 0x49, 0x8B, 0x11, 0x04, 0xD2, 0x16, 0x2E, 0x23, 0x02, 0xF8, 0x4E};

      char *ether_mbuf_offset = rte_pktmbuf_mtod_offset(pkt[i], char*, 0);
      // char *ether_mbuf_offset2 = rte_pktmbuf_mtod_offset(pkt[i], char*, sizeof(rte_ether_hdr));
      // char *ether_mbuf_offset3 = rte_pktmbuf_mtod_offset(pkt[i], char*, sizeof(rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
      char *ether_mbuf_offset4 = rte_pktmbuf_mtod_offset(pkt[i], char*, 42) ;

      rte_memcpy(ether_mbuf_offset, ary, 42);
      // rte_memcpy(ether_mbuf_offset2, &hdr, sizeof(struct rte_ipv4_hdr));
      // rte_memcpy(ether_mbuf_offset3, &hdr_udp, sizeof(struct rte_udp_hdr));
      rte_memcpy(ether_mbuf_offset4, &msg, sizeof(T));

      if (false) {
        rte_pktmbuf_dump(stdout, pkt[i], pkt[i]->pkt_len);
      }
    }
    burst_number++;

    // Send burst of TX packets
    int sent = 0;
    uint16_t nb_tx;
    while(sent < m_burst_size)
    {
      nb_tx = rte_eth_tx_burst(port, lid-1, pkt, m_burst_size - sent);
      sent += nb_tx;
      num_frames += nb_tx;
    }

    // Free any unsent packets
    if (unlikely(nb_tx < m_burst_size))
    {
      uint16_t buf;
      for (buf = nb_tx; buf < m_burst_size; buf++)
      {
        rte_pktmbuf_free(pkt[buf]);
      }
    }
    rte_eth_tx_done_cleanup(port, lid-1, 0);
  }
  return 0;
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
  int argc = 0;
  std::vector<char*> v{"test"};
  ealutils::init_eal(argc, v.data());

  std::map<int, std::unique_ptr<rte_mempool>> m;
  ealutils::port_init(0, 0, m_number_of_cores, m);
}

void
NICSender::do_configure(const data_t& args)
{
    module_conf_t cfg = args.get<module_conf_t>();

    m_burst_size = cfg.burst_size;
    m_number_of_cores = cfg.number_of_cores;
    m_rate = cfg.rate;
    m_frontend_type = cfg.frontend_type;

    for (auto& [id, ips]: cfg.core_list) {
      m_core_map[id] = ips;
      std::vector<uint32_t> vips;
      for (auto& ip : ips) {
        size_t ind = 0, current_ind = 0;
        std::vector<uint8_t> v;
        for (int i = 0; i < 4; ++i) {
          v.push_back(std::stoi(ip.substr(current_ind, ip.size() - current_ind), &ind));
          current_ind += ind + 1;
        }
        vips.push_back(RTE_IPV4(v[0], v[1], v[2], v[3]));
      }
      m_core_map32[id] = vips;
    }


    dpdk_configure();
}

void
NICSender::do_start(const data_t& args)
{
  m_run_mark.store(true);
  lcore_args largs = {this};
  if (m_frontend_type == "tde") {
    auto fun = &lcore_main<detdataformats::tde::TDE16Frame>;
    for (auto& [id, _] : m_core_map) {
        TLOG() << "Starting core " << id;
        rte_eal_remote_launch(fun, reinterpret_cast<void*>(&largs), id);
    }
  }

  // rte_eal_remote_launch( (lcore_function_t*)(&NICSender::lcore_main), this, 2);
}

void
NICSender::do_stop(const data_t& args)
{
  TLOG() << "Stopping on core " << rte_lcore_id();
  m_run_mark.store(false);
  std::this_thread::sleep_for(std::chrono::seconds(5));
  rte_eal_wait_lcore(1);
  rte_eal_cleanup();
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
