/**
 * @file Utils.hpp Utility functions for UDP packets and payloads
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_UTILS_HPP_
#define DPDKLIBS_SRC_UTILS_HPP_

#include <rte_mbuf.h>
#include <rte_ether.h>

#include "IPV4UDPPacket.hpp"

#include <cstdint>
#include <string>
#include <iostream>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

uint16_t get_payload_size_udp_hdr(struct rte_udp_hdr * udp_hdr);
uint16_t get_payload_size(struct ipv4_udp_packet_hdr * ipv4_udp_hdr);

rte_be32_t ip_address_dotdecimal_to_binary(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4);
struct ipaddr ip_address_binary_to_dotdecimal(rte_le32_t binary_ipv4_address);

std::string get_ipv4_decimal_addr_str (struct ipaddr ipv4_address);

char* get_udp_payload(struct rte_mbuf *mbuf);

//void dump_udp_header(struct ipv4_udp_packet_hdr * pkt);
std::string get_udp_header_str(struct rte_mbuf *mbuf);

std::string get_udp_packet_str(struct rte_mbuf *mbuf);

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_UTILS_HPP_
