#include <rte_ethdev.h>

#include "dpdklibs/ipv4_addr.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"

#include "detdataformats/DAQEthHeader.hpp"

#define IPV4_VERSION    4

//DEFAULT_TTL             = 8 -> TTL on wireshark 5
//DEFAULT_TTL             = 5 -> TTL on wireshark 2
//DEFAULT_TTL             = 4 -> TTL on wireshark 1

namespace dunedaq {
namespace dpdklibs {
namespace udp {

enum {
    MIN_TOS                 = 0,
    DEFAULT_TOS             = MIN_TOS,
    DEFAULT_TTL             = 8
};
//// Defined as static in DPDK
static int8_t get_xdigit(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}


bool get_ether_addr6(const char *s0, struct rte_ether_addr *ea)
{
    const char *s = s0;
    int i;

    for (i = 0; i < RTE_ETHER_ADDR_LEN; i++) {
        int8_t x;

        x = get_xdigit(*s++);
        if (x < 0)
            return false;

        ea->addr_bytes[i] = x << 4;
        x = get_xdigit(*s++);
        if (x < 0)
            return false;
        ea->addr_bytes[i] |= x;

        if (i < RTE_ETHER_ADDR_LEN - 1 &&
            *s++ != ':')
            return false;
    }

    /* return true if at end of string */
    return *s == '\0';
}


rte_le16_t
packet_fill(struct ipv4_udp_packet_hdr * packet_hdr) 
{
    //rte_le16_t total_length = 64;
    //char message[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";


    rte_le16_t total_length = 2000;
    char message[] = "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE";



    char * payload = (char *)(packet_hdr + 1); 
    memcpy(payload, message, total_length);
    return total_length;
}


/**************************************************************************//**
 *
 * pktgen_udp_hdr_ctor - UDP header constructor routine.
 *
 * DESCRIPTION
 * Construct the UDP header in a packer buffer.
 *
 * RETURNS: next header location
 *
 * SEE ALSO:
 */
void
pktgen_udp_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr, rte_le16_t packet_len, int sport, int dport)
{
    packet_hdr->udp_hdr.dgram_len = rte_cpu_to_be_16(packet_len);

    packet_hdr->udp_hdr.src_port = rte_cpu_to_be_16(sport);
    packet_hdr->udp_hdr.dst_port = rte_cpu_to_be_16(dport);

    packet_hdr->udp_hdr.dgram_cksum = 0; // checksum must be set to 0 in the L4 header by the caller.
    //TODO  I think that the payload must be already there and continuous in memory..
    return ;
}

/**************************************************************************//**
 *
 * pktgen_ipv4_ctor - Construct the IPv4 header for a packet
 *
 * DESCRIPTION
 * Constructor for the IPv4 header for a given packet.
 *
 * RETURNS: N/A
 *
 * SEE ALSO:
 */
void
pktgen_ipv4_ctor(struct ipv4_udp_packet_hdr * packet_hdr, rte_le16_t packet_len, const std::string& src_ip_addr, const std::string& dst_ip_addr)
{
    /* IPv4 Header constructor */

    /* Zero out the header space */
    memset((char *) &packet_hdr->ipv4_hdr, 0, sizeof(struct rte_ipv4_hdr));

    IpAddr src(src_ip_addr);
    IpAddr dst(dst_ip_addr);
    
    struct ipaddr src_reversed_order;
    src_reversed_order.addr_bytes[3] = src.addr_bytes[0];
    src_reversed_order.addr_bytes[2] = src.addr_bytes[1];
    src_reversed_order.addr_bytes[1] = src.addr_bytes[2];
    src_reversed_order.addr_bytes[0] = src.addr_bytes[3];

    struct ipaddr dst_reversed_order;
    dst_reversed_order.addr_bytes[3] = dst.addr_bytes[0];
    dst_reversed_order.addr_bytes[2] = dst.addr_bytes[1];
    dst_reversed_order.addr_bytes[1] = dst.addr_bytes[2];
    dst_reversed_order.addr_bytes[0] = dst.addr_bytes[3];
    
    rte_le32_t src_addr_ser = *((rte_le32_t *) &src_reversed_order);
    rte_le32_t dst_addr_ser = *((rte_le32_t *) &dst_reversed_order);

    packet_hdr->ipv4_hdr.src_addr = rte_cpu_to_be_32(src_addr_ser);
    packet_hdr->ipv4_hdr.dst_addr = rte_cpu_to_be_32(dst_addr_ser);

    packet_hdr->ipv4_hdr.version_ihl = (IPV4_VERSION << 4) | (sizeof(struct rte_ipv4_hdr) / 4);

    packet_hdr->ipv4_hdr.total_length = rte_cpu_to_be_16(packet_len); // Payload + udp_hdr + iphdr
    packet_hdr->ipv4_hdr.time_to_live = DEFAULT_TTL;
    packet_hdr->ipv4_hdr.type_of_service = DEFAULT_TOS;

    // https://perso.telecom-paristech.fr/drossi/paper/rossi17ipid.pdf 
    packet_hdr->ipv4_hdr.packet_id = rte_cpu_to_be_16(0); // I put a constant here..
    packet_hdr->ipv4_hdr.fragment_offset = 0;
    packet_hdr->ipv4_hdr.next_proto_id = IPPROTO_UDP;

    packet_hdr->ipv4_hdr.hdr_checksum = 0;
    //packet_hdr->ipv4_hdr.hdr_checksum = rte_ipv4_cksum(&packet_hdr->ipv4_hdr);

    //UDP checksum
    packet_hdr->udp_hdr.dgram_cksum = 0;
    //packet_hdr->udp_hdr.dgram_cksum = rte_ipv4_udptcp_cksum(&packet_hdr->ipv4_hdr, (void *) &packet_hdr->udp_hdr);
    //if (packet_hdr->udp_hdr.dgram_cksum == 0) {
    //    packet_hdr->udp_hdr.dgram_cksum = 0xFFFF;
    //}
    return ;
}


/**************************************************************************//**
 *
 * pktgen_ether_hdr_ctor - Ethernet header constructor routine.
 *
 * DESCRIPTION
 * Construct the ethernet header for a given packet buffer.
 *
 * RETURNS: Pointer to memory after the ethernet header.
 *
 * SEE ALSO:
 */
void
pktgen_ether_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr, const std::string& router_mac_address, const int port_id)
{
    //pg_ether_addr_copy(&pkt->eth_src_addr, &eth->src_addr);
    //pg_ether_addr_copy(&pkt->eth_dst_addr, &eth->dst_addr);

    /* src and dest addr */
    // See definition dpdk-20.08/lib/librte_net/rte_ether.c is defined as static -> not exposed to the lib... redefining here
 
  get_ether_addr6(router_mac_address.c_str(), &packet_hdr->eth_hdr.dst_addr);

    rte_eth_macaddr_get(port_id, &packet_hdr->eth_hdr.src_addr);

    /* normal ethernet header */
    packet_hdr->eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    return;
}



void construct_packets_for_burst(const int port_id, const std::string& dst_mac_addr, const int payload_bytes, const int burst_size, rte_mbuf** bufs) { 
  struct ipv4_udp_packet_hdr packet_hdr;

  constexpr int eth_header_bytes = 14;
  constexpr int udp_header_bytes = 8;
  constexpr int ipv4_header_bytes = 20;

  int eth_packet_bytes = eth_header_bytes + ipv4_header_bytes + udp_header_bytes + sizeof(detdataformats::DAQEthHeader) + payload_bytes; 
  int ipv4_packet_bytes = eth_packet_bytes - eth_header_bytes;
  int udp_datagram_bytes = ipv4_packet_bytes - ipv4_header_bytes;

  // Get info for the ethernet header (protocol stack level 2)
  pktgen_ether_hdr_ctor(&packet_hdr, dst_mac_addr, port_id);
  
  // Get info for the internet header (protocol stack level 3)
  pktgen_ipv4_ctor(&packet_hdr, ipv4_packet_bytes);

  // Get info for the UDP header (protocol stack level 4)
  pktgen_udp_hdr_ctor(&packet_hdr, udp_datagram_bytes);

  detdataformats::DAQEthHeader daqethheader_obj;
  set_daqethheader_test_values(daqethheader_obj);

  void* dataloc = nullptr;
  for (int i_pkt = 0; i_pkt < burst_size; ++i_pkt) {

    dataloc = rte_pktmbuf_mtod(bufs[i_pkt], char*);
    rte_memcpy(dataloc, &packet_hdr, sizeof(packet_hdr));

    dataloc = rte_pktmbuf_mtod_offset(bufs[i_pkt], char*, sizeof(packet_hdr));
    rte_memcpy(dataloc, &daqethheader_obj, sizeof(daqethheader_obj));

    bufs[i_pkt]->pkt_len = eth_packet_bytes;
    bufs[i_pkt]->data_len = eth_packet_bytes;
  }
}


} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq
