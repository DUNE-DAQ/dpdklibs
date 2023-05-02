#include <rte_ethdev.h>

#include "dpdklibs/ipv4_addr.hpp"
#include "dpdklibs/udp/Utils.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"

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
pktgen_ether_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr, const std::string& router_mac_address)
{
    //pg_ether_addr_copy(&pkt->eth_src_addr, &eth->src_addr);
    //pg_ether_addr_copy(&pkt->eth_dst_addr, &eth->dst_addr);

    /* src and dest addr */
    // See definition dpdk-20.08/lib/librte_net/rte_ether.c is defined as static -> not exposed to the lib... redefining here
 
  get_ether_addr6(router_mac_address.c_str(), &packet_hdr->eth_hdr.dst_addr);

    uint8_t port_id = 0;
    rte_eth_macaddr_get(port_id, &packet_hdr->eth_hdr.src_addr);

    /* normal ethernet header */
    packet_hdr->eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    return;
}



/**************************************************************************//**
 
 * pktgen_packet_ctor - Construct a complete packet with all headers and data.
 *
 * DESCRIPTION
 * Construct a packet type based on the arguments passed with all headers.
 *
 * RETURNS: N/A
 *
 * SEE ALSO:
 */

rte_le16_t
pktgen_packet_ctor(struct ipv4_udp_packet_hdr * packet_hdr)
{
    //pkt_seq_t *pkt = &info->seq_pkt[seq_idx];
    //struct pg_ether_hdr *eth = (struct pg_ether_hdr *) & pkt->hdr.eth;
    //char *l3_hdr = (char *) & eth[1]; /* Point to l3 hdr location for GRE header */

    // fill the data first
    ///* Fill in the pattern for data space. */
    //pktgen_fill_pattern((uint8_t *)&pkt->hdr,
    //                    (sizeof(pkt_hdr_t) + sizeof(pkt->pad)),
    //                    info->fill_pattern_type, info->user_pattern);

    rte_le16_t packet_len = 0;
    packet_len += packet_fill(packet_hdr); //TODO should return the size of the payload only


    //l3_hdr = pktgen_ether_hdr_ctor(info, pkt, eth);

    pktgen_ether_hdr_ctor(packet_hdr);

    //if (likely(pkt->ethType == PG_ETHER_TYPE_IPv4)) {
    //    if (pkt->ipProto == PG_IPPROTO_UDP) {
    //    }
    //}

    /* Construct the UDP header */
    pktgen_udp_hdr_ctor(packet_hdr, packet_len + sizeof(struct rte_udp_hdr));

    /* IPv4 Header constructor */
    pktgen_ipv4_ctor(packet_hdr, packet_len + sizeof(struct rte_udp_hdr) + sizeof(struct rte_ipv4_hdr));

    return packet_len;
}

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq
