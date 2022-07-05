/**
 * @file PacketCtor.hpp Functions to construct 
 * IPV4 UDP Packets and headers
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_UDP_PACKETCTOR_HPP_
#define DPDKLIBS_SRC_UDP_PACKETCTOR_HPP_

#include <rte_byteorder.h>
#include "IPV4UDPPacket.hpp"

namespace dunedaq {
namespace dpdklibs {
namespace udp {

rte_le16_t packet_fill(struct ipv4_udp_packet_hdr * packet_hdr);

void pktgen_udp_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr, rte_le16_t packet_len);

void pktgen_ipv4_ctor(struct ipv4_udp_packet_hdr * packet_hdr, rte_le16_t packet_len);

void pktgen_ether_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr);

rte_le16_t pktgen_packet_ctor(struct ipv4_udp_packet_hdr * packet_hdr);

rte_le16_t ethr_packet_ctor(struct ether_packet * packet);

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_UDP_PACKETCTOR_HPP_
