/**
 * @file IPV4UDPPacket.hpp IPV4 UDP Packet structure
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_UDP_IPV4UDPPACKET_HPP_
#define DPDKLIBS_SRC_UDP_IPV4UDPPACKET_HPP_

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <ostream>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

struct ipaddr {
    uint8_t addr_bytes[4];
};

struct ipv4_udp_packet_hdr {
    struct rte_ether_hdr eth_hdr;
    // l3 header
    /**
     * Ethernet header: Contains the destination address, source address
     * and frame type.
     */
    //struct rte_ether_hdr {
    //        struct rte_ether_addr d_addr; /**< Destination address. */
    //        struct rte_ether_addr s_addr; /**< Source address. */
    //        uint16_t ether_type;      /**< Frame type. */
    //} __rte_aligned(2);
    struct rte_ipv4_hdr ipv4_hdr;
    // l3 header
    /**
     * IPv4 Header
     */
    //struct rte_ipv4_hdr {
    //    uint8_t  version_ihl;       /**< version and header length */
    //    uint8_t  type_of_service;   /**< type of service */
    //    rte_be16_t total_length;    /**< length of packet */
    //    rte_be16_t packet_id;       /**< packet ID */
    //    rte_be16_t fragment_offset; /**< fragmentation offset */
    //    uint8_t  time_to_live;      /**< time to live */
    //    uint8_t  next_proto_id;     /**< protocol ID */
    //    rte_be16_t hdr_checksum;    /**< header checksum */
    //    rte_be32_t src_addr;        /**< source address */
    //    rte_be32_t dst_addr;        /**< destination address */
    //} __rte_packed;
    struct rte_udp_hdr udp_hdr;
    /**
     * UDP Header
     */
    //struct rte_udp_hdr {
    //        rte_be16_t src_port;    /**< UDP source port. */
    //        rte_be16_t dst_port;    /**< UDP destination port. */
    //        rte_be16_t dgram_len;   /**< UDP datagram length */
    //        rte_be16_t dgram_cksum; /**< UDP datagram checksum */
    //} __rte_packed;
} __rte_packed;

  inline std::ostream& operator<<(std::ostream& o, const ipv4_udp_packet_hdr& h) {
    o << "\n" << std::dec << sizeof(h) << " bytes of ipv4_udp_packet_hdr contains the following:\n";
    o << "Ethernet header (" << sizeof(rte_ether_hdr) << " bytes): \n";
    o << "Destination address: ";
    
    for (int i = 0; i < RTE_ETHER_ADDR_LEN; ++i) {
      o << "0x" << std::hex << static_cast<int>(h.eth_hdr.dst_addr.addr_bytes[i]) << " ";
    }

    o << "\nSource address: ";

    for (int i = 0; i < RTE_ETHER_ADDR_LEN; ++i) {
      o << "0x" << std::hex << static_cast<int>(h.eth_hdr.src_addr.addr_bytes[i]) << " ";
    }

    o << "\nEther type (Big Endian to CPU): " << rte_be_to_cpu_32(h.eth_hdr.ether_type);

    o << "\n\n";
    o << "Internet header (" << std::dec << sizeof(rte_ipv4_hdr) << std::hex << " bytes): \n";
    o << "Total length: " << std::dec << rte_be_to_cpu_16(h.ipv4_hdr.total_length) << std::hex;
    o << "\nSource address: " << rte_be_to_cpu_32(h.ipv4_hdr.src_addr);
    o << "\nDestination address: " << rte_be_to_cpu_32(h.ipv4_hdr.dst_addr);
    
    return o;
  }
  
struct ipv4_udp_packet {
    struct ipv4_udp_packet_hdr hdr;
#warning RS FIXME -> Hardcoded IPV4 UDP packet payload size!
    char payload[8000]; // TODO jumbo Mind the padding
};

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_UDP_IPV4UDPPACKET_HPP_
