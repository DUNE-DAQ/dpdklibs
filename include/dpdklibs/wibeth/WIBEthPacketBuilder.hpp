/**
 * @file WIBEthPacketBuilder.hpp
 *
 * Construction of Ethernet/IPv4/UDP headers around a WIBEthFrame for DPDK TX.
 * Free of DPDK runtime calls, so the byte construction is unit-testable
 * without EAL or a NIC.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_WIBETH_WIBETHPACKETBUILDER_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_WIBETH_WIBETHPACKETBUILDER_HPP_

#include "detdataformats/DAQEthHeader.hpp"
#include "dpdklibs/udp/IPV4UDPPacket.hpp"
#include "fddetdataformats/WIBEthFrame.hpp"

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace dunedaq::dpdklibs::wibeth {

constexpr std::size_t kEthernetHeaderBytes = sizeof(rte_ether_hdr);
constexpr std::size_t kIPv4HeaderBytes = sizeof(rte_ipv4_hdr);
constexpr std::size_t kUDPHeaderBytes = sizeof(rte_udp_hdr);
constexpr std::size_t kPacketHeaderBytes = sizeof(dunedaq::dpdklibs::udp::ipv4_udp_packet_hdr);
constexpr std::size_t kDAQEthHeaderBytes = sizeof(dunedaq::detdataformats::DAQEthHeader);
constexpr std::size_t kWIBEthFrameBytes = sizeof(dunedaq::fddetdataformats::WIBEthFrame);
constexpr std::size_t kEthernetPacketBytes = kEthernetHeaderBytes + kIPv4HeaderBytes + kUDPHeaderBytes + kWIBEthFrameBytes;
constexpr std::size_t kIPv4TotalBytes = kIPv4HeaderBytes + kUDPHeaderBytes + kWIBEthFrameBytes;
constexpr std::size_t kUDPDatagramBytes = kUDPHeaderBytes + kWIBEthFrameBytes;
constexpr std::uint16_t kWIBEthBlockLength = static_cast<std::uint16_t>((kWIBEthFrameBytes / sizeof(std::uint64_t)) - 1);
constexpr std::uint64_t kDefaultTimestampStep = 2048;
constexpr double kDefaultRateHz = 62500000.0 / static_cast<double>(kDefaultTimestampStep);

static_assert(kEthernetHeaderBytes == 14, "Unexpected Ethernet header size");
static_assert(kIPv4HeaderBytes == 20, "Unexpected IPv4 header size");
static_assert(kUDPHeaderBytes == 8, "Unexpected UDP header size");
static_assert(kPacketHeaderBytes == 42, "Unexpected Ethernet/IPv4/UDP header size");
static_assert(kDAQEthHeaderBytes == 16, "Unexpected DAQEthHeader size");
static_assert(kWIBEthFrameBytes == 7200, "Unexpected WIBEthFrame size");
static_assert(kEthernetPacketBytes == 7242, "Unexpected complete Ethernet packet size");
static_assert(kIPv4TotalBytes == 7228, "Unexpected IPv4 total length");
static_assert(kUDPDatagramBytes == 7208, "Unexpected UDP datagram length");
static_assert(kWIBEthBlockLength == 899, "Unexpected DAQEthHeader block_length");

struct PacketConfig
{
  rte_ether_addr dst_mac{};
  rte_ether_addr src_mac{};
  rte_be32_t src_ip = 0;
  rte_be32_t dst_ip = 0;
  std::uint16_t src_port = 55677;
  std::uint16_t dst_port = 55678;
  std::uint16_t packet_id = 0;
};

struct HeaderPatch
{
  bool set_det_id = false;
  std::uint64_t det_id = 0;
  bool set_seq_id = false;
  std::uint64_t seq_id = 0;
  bool force_block_length = true;
};

// Throws std::runtime_error if addr is not a dotted-quad IPv4 address.
rte_be32_t
parse_ipv4_addr(const std::string& addr);

// Rewrites the DAQEthHeader fields selected by patch, in place.  wibeth_frame
// may have any alignment.
void
patch_daq_header(void* wibeth_frame, const HeaderPatch& patch);

// Writes kEthernetPacketBytes to ethernet_packet: the 42-byte
// Ethernet/IPv4/UDP header, then the WIBEthFrame, then the header patch.
// ethernet_packet may have any alignment.
void
construct_packet(void* ethernet_packet, const void* wibeth_frame, const PacketConfig& cfg, const HeaderPatch& patch = {});

} // namespace dunedaq::dpdklibs::wibeth

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_WIBETH_WIBETHPACKETBUILDER_HPP_
