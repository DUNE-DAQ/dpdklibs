/**
 * @file WIBEthPacketBuilder.cpp Implementation of the WIBEth packet builder.
 */
#include "dpdklibs/wibeth/WIBEthPacketBuilder.hpp"

#include "detdataformats/DAQEthHeader.hpp"

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace dunedaq::dpdklibs::wibeth {

rte_be32_t
parse_ipv4_addr(const std::string& addr)
{
  in_addr parsed{};
  if (inet_pton(AF_INET, addr.c_str(), &parsed) != 1) {
    throw std::runtime_error("Invalid IPv4 address: " + addr);
  }
  return static_cast<rte_be32_t>(parsed.s_addr);
}

void
patch_daq_header(void* wibeth_frame, const HeaderPatch& patch)
{
  // The frame is at Ethernet offset 42, which does not satisfy the 8-byte
  // alignment of DAQEthHeader.  Patch an aligned local copy and copy it back
  // rather than writing 64-bit bitfields through an unaligned pointer.
  dunedaq::detdataformats::DAQEthHeader daq_header;
  std::memcpy(&daq_header, wibeth_frame, sizeof(daq_header));
  if (patch.set_det_id) {
    daq_header.det_id = patch.det_id;
  }
  if (patch.set_seq_id) {
    daq_header.seq_id = patch.seq_id % 4096;
  }
  if (patch.force_block_length) {
    daq_header.block_length = kWIBEthBlockLength;
  }
  std::memcpy(wibeth_frame, &daq_header, sizeof(daq_header));
}

void
construct_packet(void* ethernet_packet, const void* wibeth_frame, const PacketConfig& cfg, const HeaderPatch& patch)
{
  // Build the 42-byte Ethernet/IPv4/UDP header in a local object and memcpy it
  // into place.  The destination may have any alignment (mbuf data offset, test
  // buffer), so caller storage is never written through a typed pointer.
  // Value-initialization covers the whole object; the packed header type has no
  // padding.
  dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr header{};

  header.eth_hdr.dst_addr = cfg.dst_mac;
  header.eth_hdr.src_addr = cfg.src_mac;
  header.eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

  header.ipv4_hdr.version_ihl = (4 << 4) | (kIPv4HeaderBytes / 4);
  header.ipv4_hdr.type_of_service = 0;
  header.ipv4_hdr.total_length = rte_cpu_to_be_16(static_cast<std::uint16_t>(kIPv4TotalBytes));
  header.ipv4_hdr.packet_id = rte_cpu_to_be_16(cfg.packet_id);
  header.ipv4_hdr.fragment_offset = rte_cpu_to_be_16(0);
  header.ipv4_hdr.time_to_live = 64;
  header.ipv4_hdr.next_proto_id = IPPROTO_UDP;
  header.ipv4_hdr.hdr_checksum = 0;
  header.ipv4_hdr.src_addr = cfg.src_ip;
  header.ipv4_hdr.dst_addr = cfg.dst_ip;
  header.ipv4_hdr.hdr_checksum = rte_ipv4_cksum(&header.ipv4_hdr);

  header.udp_hdr.src_port = rte_cpu_to_be_16(cfg.src_port);
  header.udp_hdr.dst_port = rte_cpu_to_be_16(cfg.dst_port);
  header.udp_hdr.dgram_len = rte_cpu_to_be_16(static_cast<std::uint16_t>(kUDPDatagramBytes));
  header.udp_hdr.dgram_cksum = 0;

  std::memcpy(ethernet_packet, &header, kPacketHeaderBytes);

  auto* frame = static_cast<std::uint8_t*>(ethernet_packet) + kPacketHeaderBytes;
  std::memcpy(frame, wibeth_frame, kWIBEthFrameBytes);
  patch_daq_header(frame, patch);
}

} // namespace dunedaq::dpdklibs::wibeth
