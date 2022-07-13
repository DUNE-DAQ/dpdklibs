/**
 * @file Utils.cpp Utility functions' implementations
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/udp/Utils.hpp"
#include <algorithm>
#include <cstring>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

inline void
dump_to_buffer(const char* data,
               std::size_t size,
               void* buffer,
               uint32_t buffer_pos, // NOLINT
               const std::size_t& buffer_size)
{
  auto bytes_to_copy = size; // NOLINT
  while (bytes_to_copy > 0) {
    auto n = std::min(bytes_to_copy, buffer_size - buffer_pos); // NOLINT
    std::memcpy(static_cast<char*>(buffer) + buffer_pos, data, n);
    buffer_pos += n;
    bytes_to_copy -= n;
    if (buffer_pos == buffer_size) {
      buffer_pos = 0;
    }
  }
}

uint16_t
get_payload_size_udp_hdr(struct rte_udp_hdr * udp_hdr)
{
  return rte_be_to_cpu_16(udp_hdr->dgram_len) - sizeof(struct rte_udp_hdr);
}

uint16_t
get_payload_size(struct ipv4_udp_packet_hdr * ipv4_udp_hdr)
{
  return rte_be_to_cpu_16(ipv4_udp_hdr->udp_hdr.dgram_len) - sizeof(struct rte_udp_hdr);
}

rte_be32_t
ip_address_dotdecimal_to_binary(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4)
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

void
print_ipv4_decimal_addr(struct ipaddr ipv4_address) {
  printf("%i.%i.%i.%i",
        ipv4_address.addr_bytes[4],
        ipv4_address.addr_bytes[3],
        ipv4_address.addr_bytes[1],
        ipv4_address.addr_bytes[0]);
}

char *
get_udp_payload(struct rte_mbuf *mbuf, uint16_t dump_mode)
{
  struct ipv4_udp_packet_hdr * udp_packet = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr *);
  //dump_udp_header(udp_packet);
  uint16_t payload_size = get_payload_size(udp_packet);
  char* payload = (char *)(udp_packet + 1);

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
}

void 
dump_udp_header(struct ipv4_udp_packet_hdr * pkt)
{
  printf("------ start of packet ----- \n");
  //static void
  //print_ethaddr(const char *name, struct rte_ether_addr *eth_addr)
  //{
  //    char buf[RTE_ETHER_ADDR_FMT_SIZE];
  //    rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
  //    printf("%s%s", name, buf);
  //} 
  printf("dst mac addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
         pkt->eth_hdr.dst_addr.addr_bytes[0],
         pkt->eth_hdr.dst_addr.addr_bytes[1],
         pkt->eth_hdr.dst_addr.addr_bytes[2],
         pkt->eth_hdr.dst_addr.addr_bytes[3],
         pkt->eth_hdr.dst_addr.addr_bytes[4],
         pkt->eth_hdr.dst_addr.addr_bytes[5]);
  printf("src mac addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
         pkt->eth_hdr.src_addr.addr_bytes[0],
         pkt->eth_hdr.src_addr.addr_bytes[1],
         pkt->eth_hdr.src_addr.addr_bytes[2],
         pkt->eth_hdr.src_addr.addr_bytes[3],
         pkt->eth_hdr.src_addr.addr_bytes[4],
         pkt->eth_hdr.src_addr.addr_bytes[5]);
  printf("ethtype: %i\n", pkt->eth_hdr.ether_type);

  printf("------ IP header ----- \n");
  printf("ipv4 version: %i\n", pkt->ipv4_hdr.version_ihl);
  printf("ipv4 type_of_service %i\n", pkt->ipv4_hdr.type_of_service);
  printf("ipv4 total lenght: %i\n", rte_be_to_cpu_16(pkt->ipv4_hdr.total_length));
  printf("ipv4 packet_id %i\n", pkt->ipv4_hdr.packet_id);
  printf("ipv4 fragment_offset %i\n", pkt->ipv4_hdr.fragment_offset);
  printf("ipv4 time_to_live %i\n", pkt->ipv4_hdr.time_to_live);
  printf("ipv4 next_proto_id %i\n", pkt->ipv4_hdr.next_proto_id);
  printf("ipv4 checksum: %i\n", rte_be_to_cpu_16(pkt->ipv4_hdr.hdr_checksum));
  printf("src_addr: ");
  print_ipv4_decimal_addr(ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.src_addr)));
  printf("\n");
  printf("dst_addr: ");
  print_ipv4_decimal_addr(ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.dst_addr)));
  printf("\n");

  printf("------ UDP header ----- \n");
  printf("UDP src_port: %i\n", rte_be_to_cpu_16(pkt->udp_hdr.src_port));
  printf("UDP dst_port: %i\n", rte_be_to_cpu_16(pkt->udp_hdr.dst_port));
  printf("UDP len: %i\n", rte_be_to_cpu_16(pkt->udp_hdr.dgram_len));
  printf("UDP checksum: %i\n", rte_be_to_cpu_16(pkt->udp_hdr.dgram_cksum));

  char* payload = (char *)(pkt);
  uint byte;
  for (byte = 0; byte < rte_be_to_cpu_16(pkt->udp_hdr.dgram_len); byte++) {
    printf("%02x ", *(payload + byte) & 0xFF);
    //printf("%s", (payload + byte));
  }
  printf("\n");
  return;
}

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq
