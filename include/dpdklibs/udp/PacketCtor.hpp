/**
 * @file PacketCtor.hpp Functions to construct 
 * IPV4 UDP Packets and headers
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_UDP_PACKETCTOR_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_UDP_PACKETCTOR_HPP_

#include <rte_byteorder.h>
#include "IPV4UDPPacket.hpp"

#include "detdataformats/DAQEthHeader.hpp"

#include <string>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

rte_le16_t packet_fill(struct ipv4_udp_packet_hdr * packet_hdr);

  void pktgen_udp_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr, rte_le16_t packet_len, int sport = 55677, int dport = 55678);

  void pktgen_ipv4_ctor(struct ipv4_udp_packet_hdr * packet_hdr, rte_le16_t packet_len, const std::string& src_ip_addr = "0.0.0.0", const std::string& dst_ip_addr = "0.0.0.0");

  void pktgen_ether_hdr_ctor(struct ipv4_udp_packet_hdr * packet_hdr, const std::string& dst_mac_address = "0a:00:10:c2:15:c1", const int port_id = 0);

/* Convert 00:11:22:33:44:55 to ethernet address */
  bool get_ether_addr6(const char *s0, struct rte_ether_addr *ea);

  // n.b. The memory pool which bufs is pointing to must be allocated, otherwise you'll get a crash
  void construct_packets_for_burst(const int port_id, const std::string& dst_mac_addr, const int payload_bytes, const int burst_size, rte_mbuf** bufs);
  
} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_PACKETCTOR_HPP_
