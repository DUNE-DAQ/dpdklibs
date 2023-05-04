/**
 * @file Utils.cpp UDP related utility functions' implementations
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/udp/Utils.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

std::uint16_t
get_payload_size_udp_hdr(struct rte_udp_hdr * udp_hdr)
{
  return rte_be_to_cpu_16(udp_hdr->dgram_len) - sizeof(struct rte_udp_hdr);
}

std::uint16_t
get_payload_size(struct ipv4_udp_packet_hdr * ipv4_udp_hdr)
{
  return rte_be_to_cpu_16(ipv4_udp_hdr->udp_hdr.dgram_len) - sizeof(struct rte_udp_hdr);
}

std::uint16_t
get_payload_size_mbuf(struct rte_mbuf *mbuf)
{
  struct ipv4_udp_packet_hdr * ipv4_udp_hdr = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr *);
  return rte_be_to_cpu_16(ipv4_udp_hdr->udp_hdr.dgram_len) - sizeof(struct rte_udp_hdr);
}

rte_be32_t
ip_address_dotdecimal_to_binary(std::uint8_t byte1, std::uint8_t byte2, 
                                std::uint8_t byte3, std::uint8_t byte4)
{ 
  rte_le32_t ip_address = (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
  return rte_cpu_to_be_32(ip_address);
}


struct ipaddr
ip_address_binary_to_dotdecimal(rte_le32_t binary_ipv4_address)
{
  struct ipaddr addr;
  memcpy(&addr, &binary_ipv4_address, sizeof(rte_le32_t));
  return addr;
}

std::string
get_ipv4_decimal_addr_str(struct ipaddr ipv4_address) {
  std::ostringstream ostrs;
  ostrs << (unsigned)ipv4_address.addr_bytes[3] << '.'
        << (unsigned)ipv4_address.addr_bytes[2] << '.'
        << (unsigned)ipv4_address.addr_bytes[1] << '.'
        << (unsigned)ipv4_address.addr_bytes[0];
  return ostrs.str();
  /*printf("%i.%i.%i.%i",
        ipv4_address.addr_bytes[3],
        ipv4_address.addr_bytes[2],
        ipv4_address.addr_bytes[1],
        ipv4_address.addr_bytes[0]);
  */
}

char *
get_udp_payload(struct rte_mbuf *mbuf)
{
  struct ipv4_udp_packet_hdr * udp_packet = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr *);
  //dump_udp_header(udp_packet);
  //uint16_t payload_size = get_payload_size(udp_packet);
  char* payload = (char *)(udp_packet + 1);
  return payload;

  /*
  if (dump_mode == 10) {
    return payload;
  }

  if (dump_mode == 0 || dump_mode == 3) {
    printf("UDP Payload size: %i\n", payload_size);
    uint byte;
    for (byte = 0; byte < payload_size; byte++) {
      printf("%02x ", *(payload + byte) & 0xFF);
      //printf("%s", (payload + byte));
    }
    printf("\n");
  }

  if (dump_mode == 1 || dump_mode == 3) {
    printf("%s\n", payload);
  }
  return payload;
  */
}

inline void 
hex_digits_to_stream(std::ostringstream& ostrs, int value, char separator = ':', char fill = '0', int digits = 2) {
  ostrs << std::setfill(fill) << std::setw(digits) << std::hex << value << std::dec << separator;
}

std::string
//dump_udp_header(struct ipv4_udp_packet_hdr * pkt)
get_udp_header_str(struct rte_mbuf *mbuf)
{
  struct ipv4_udp_packet_hdr * pkt = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr *);
  std::ostringstream ostrs;
  ostrs << "\n------ start of packet ----- \n";
  ostrs << "dst mac addr: ";
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.dst_addr.addr_bytes[0]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.dst_addr.addr_bytes[1]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.dst_addr.addr_bytes[2]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.dst_addr.addr_bytes[3]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.dst_addr.addr_bytes[4]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.dst_addr.addr_bytes[5], '\n');
  ostrs << "src mac addr: ";
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.src_addr.addr_bytes[0]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.src_addr.addr_bytes[1]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.src_addr.addr_bytes[2]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.src_addr.addr_bytes[3]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.src_addr.addr_bytes[4]);
  hex_digits_to_stream(ostrs, (int)pkt->eth_hdr.src_addr.addr_bytes[5], '\n');
  ostrs << "ethtype: " << (unsigned)pkt->eth_hdr.ether_type << '\n';

  ostrs << "------ IP header ----- \n";
  ostrs << "ipv4 version: " << (unsigned)pkt->ipv4_hdr.version_ihl << '\n';
  ostrs << "ipv4 type_of_service: " << (unsigned)pkt->ipv4_hdr.type_of_service << '\n';
  ostrs << "ipv4 total lenght: " << (unsigned)rte_be_to_cpu_16(pkt->ipv4_hdr.total_length) << '\n';
  ostrs << "ipv4 packet_id: " << (unsigned)pkt->ipv4_hdr.packet_id << '\n';
  ostrs << "ipv4 fragment_offset: " << (unsigned)pkt->ipv4_hdr.fragment_offset << '\n';
  ostrs << "ipv4 time_to_live: " << (unsigned)pkt->ipv4_hdr.time_to_live << '\n';
  ostrs << "ipv4 next_proto_id: " << (unsigned)pkt->ipv4_hdr.next_proto_id << '\n';
  ostrs << "ipv4 checksum: " << (unsigned)rte_be_to_cpu_16(pkt->ipv4_hdr.hdr_checksum) << '\n';
  std::string srcaddr = get_ipv4_decimal_addr_str(
    ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.src_addr)));
  std::string dstaddr = get_ipv4_decimal_addr_str(
    ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.dst_addr)));
  ostrs << "src_addr: " << srcaddr << '\n';
  ostrs << "dst_addr: " << dstaddr << '\n';

  ostrs << "------ UDP header ----- \n";
  ostrs << "UDP src_port: " << (unsigned)rte_be_to_cpu_16(pkt->udp_hdr.src_port) << '\n';
  ostrs << "UDP dst_port: " << (unsigned)rte_be_to_cpu_16(pkt->udp_hdr.dst_port) << '\n';
  ostrs << "UDP len: " << (unsigned)rte_be_to_cpu_16(pkt->udp_hdr.dgram_len) << '\n';
  ostrs << "UDP checksum: " << (unsigned)rte_be_to_cpu_16(pkt->udp_hdr.dgram_cksum) << '\n';
  
  return ostrs.str();
}

std::string
get_udp_packet_str(struct rte_mbuf *mbuf)
{
  struct ipv4_udp_packet_hdr * pkt = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr *);
  char* payload = (char *)(pkt);
  std::ostringstream ostrs;
  std::uint8_t byte;
  for (byte = 0; byte < rte_be_to_cpu_16(pkt->udp_hdr.dgram_len); byte++) {
    hex_digits_to_stream(ostrs, (unsigned)(*(payload + byte)), ' ');
    //printf("%02x ", *(payload + byte) & 0xFF);
    //printf("%s", (payload + byte));
  }
  return ostrs.str();
}

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq
