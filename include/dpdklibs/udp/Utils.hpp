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

#include "detdataformats/DAQEthHeader.hpp"
#include "logging/Logging.hpp"

#include <cstdint>
#include <string>
#include <iostream>
#include <vector>
#include <utility>

namespace dunedaq {

  ERS_DECLARE_ISSUE(dpdklibs,
		    BadPacketHeaderIssue,
		    "BadPacketHeaderIssue: \"" << ers_messg << "\"",
		    ((std::string)ers_messg))
  
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

  void add_file_contents_to_vector(const std::string& filename, std::vector<char>& buffervec );
  
  std::vector<std::pair<const void*, int>> get_ethernet_packets(const std::vector<char>& buffervec);

  void set_daqethheader_test_values(detdataformats::DAQEthHeader& daqethheader_obj) noexcept;

  std::string get_rte_mbuf_str(const rte_mbuf* mbuf) noexcept;
  
} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_UTILS_HPP_
