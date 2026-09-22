/**
 * @file WIBEthPacketBuilder_test.cxx
 *
 * Byte-exact verification of the WIBEth packet builder against a reference
 * header assembled from the protocol definitions rather than from the builder:
 * the full 42-byte Ethernet/IPv4/UDP layout including checksum, payload
 * equality outside the patched DAQ header, input immutability, patch
 * semantics, and construction into a misaligned destination.
 */

#include "dpdklibs/wibeth/WIBEthPacketBuilder.hpp"
#include "fdreadoutlibs/DUNEWIBEthTypeAdapter.hpp"

#define BOOST_TEST_MODULE WIBEthPacketBuilder_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace dunedaq::dpdklibs;

namespace {

// Reference configuration, each field in the notation it is normally quoted
// in.  golden_config() hands these to the builder; expected_header() lays the
// same values out on the wire.  The two paths share no code.
constexpr std::array<std::uint8_t, 6> kDstMacBytes = { 0x6c, 0xb3, 0x11, 0x79, 0x81, 0x00 };
constexpr std::array<std::uint8_t, 6> kSrcMacBytes = { 0x6c, 0xb3, 0x11, 0x79, 0xb8, 0x6c };
constexpr std::array<std::uint8_t, 4> kSrcIpBytes = { 192, 168, 100, 1 };
constexpr std::array<std::uint8_t, 4> kDstIpBytes = { 192, 168, 100, 2 };
constexpr std::uint16_t kSrcPort = 55677;
constexpr std::uint16_t kDstPort = 55678;
constexpr std::uint16_t kPacketId = 42;

rte_ether_addr
make_mac(const std::array<std::uint8_t, 6>& bytes)
{
  rte_ether_addr addr{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    addr.addr_bytes[i] = bytes[i];
  }
  return addr;
}

// The DAQ header is at Ethernet offset 42 and is not aligned for its 64-bit
// words.  Read it through an aligned copy.
dunedaq::detdataformats::DAQEthHeader
read_daq_header(const std::uint8_t* frame_start)
{
  dunedaq::detdataformats::DAQEthHeader header;
  std::memcpy(&header, frame_start, sizeof(header));
  return header;
}

// Ones'-complement sum of 16-bit big-endian words, written independently of
// rte_ipv4_cksum, which the builder uses.  Over a valid IPv4 header including
// its checksum field the sum is 0xffff; over the same header with that field
// zero, the complement of the sum is the checksum.
std::uint16_t
ones_complement_sum(const std::uint8_t* data, std::size_t bytes)
{
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i + 1 < bytes; i += 2) {
    sum += static_cast<std::uint32_t>((data[i] << 8) | data[i + 1]);
  }
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffffU) + (sum >> 16);
  }
  return static_cast<std::uint16_t>(sum);
}

wibeth::PacketConfig
golden_config()
{
  wibeth::PacketConfig cfg;
  cfg.dst_mac = make_mac(kDstMacBytes);
  cfg.src_mac = make_mac(kSrcMacBytes);
  cfg.src_ip = wibeth::parse_ipv4_addr("192.168.100.1");
  cfg.dst_ip = wibeth::parse_ipv4_addr("192.168.100.2");
  cfg.src_port = kSrcPort;
  cfg.dst_port = kDstPort;
  cfg.packet_id = kPacketId;
  return cfg;
}

// The 42-byte header golden_config() must produce, assembled field by field
// from the protocol definitions: Ethernet II, IPv4 (RFC 791), UDP (RFC 768).
// Sizes are literals, byte order is explicit, and the checksum comes from the
// routine above.  No builder constant, no DPDK helper and no function under
// test appears here, so this is an independent reference rather than a
// restatement of the implementation.
std::array<std::uint8_t, 42>
expected_header()
{
  constexpr std::uint16_t kIPv4HeaderLen = 20;
  constexpr std::uint16_t kUDPHeaderLen = 8;
  constexpr std::uint16_t kWIBEthFrameLen = 7200;
  constexpr std::size_t kIPv4Offset = 14;
  constexpr std::size_t kChecksumOffset = kIPv4Offset + 10;

  std::array<std::uint8_t, 42> header{};
  std::size_t at = 0;
  // at() rather than operator[]: a layout that writes past the end must throw,
  // not corrupt the reference.
  const auto put8 = [&](std::uint8_t value) { header.at(at++) = value; };
  const auto put16 = [&](std::uint16_t value) {
    put8(static_cast<std::uint8_t>(value >> 8));
    put8(static_cast<std::uint8_t>(value & 0xffU));
  };
  const auto put_bytes = [&](const auto& bytes) {
    for (auto byte : bytes) {
      put8(byte);
    }
  };

  // Ethernet II: destination MAC, source MAC, EtherType 0x0800 for IPv4.
  put_bytes(kDstMacBytes);
  put_bytes(kSrcMacBytes);
  put16(0x0800);

  // IPv4: version 4 with a 5-word header, no DSCP/ECN, total length,
  // identification, no flags or fragment offset, TTL 64, protocol 17 (UDP),
  // checksum zero for now, source address, destination address.
  put8(0x45);
  put8(0x00);
  put16(static_cast<std::uint16_t>(kIPv4HeaderLen + kUDPHeaderLen + kWIBEthFrameLen));
  put16(kPacketId);
  put16(0x0000);
  put8(64);
  put8(17);
  put16(0x0000);
  put_bytes(kSrcIpBytes);
  put_bytes(kDstIpBytes);

  // UDP: source port, destination port, datagram length, checksum unused.
  put16(kSrcPort);
  put16(kDstPort);
  put16(static_cast<std::uint16_t>(kUDPHeaderLen + kWIBEthFrameLen));
  put16(0x0000);

  BOOST_REQUIRE_EQUAL(at, header.size());

  // The checksum covers the IPv4 header with its own field zero, which is the
  // state written above.
  const std::uint16_t checksum =
    static_cast<std::uint16_t>(~ones_complement_sum(&header[kIPv4Offset], kIPv4HeaderLen));
  header.at(kChecksumOffset) = static_cast<std::uint8_t>(checksum >> 8);
  header.at(kChecksumOffset + 1) = static_cast<std::uint8_t>(checksum & 0xffU);

  return header;
}

} // namespace

BOOST_AUTO_TEST_SUITE(WIBEthPacketBuilder_test)

BOOST_AUTO_TEST_CASE(GoldenHeaderPayloadAndInputImmutability)
{
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0xab, sizeof(frame.data));

  auto* wibeth_frame = reinterpret_cast<dunedaq::fddetdataformats::WIBEthFrame*>(frame.data);
  wibeth_frame->daq_header.det_id = 0;
  wibeth_frame->daq_header.crate_id = 1;
  wibeth_frame->daq_header.slot_id = 1;
  wibeth_frame->daq_header.stream_id = 0;
  wibeth_frame->daq_header.seq_id = 0;
  wibeth_frame->daq_header.block_length = 0;
  wibeth_frame->daq_header.timestamp = 123456789;

  // Copy the input so it can be compared after construction.
  std::vector<std::uint8_t> input_snapshot(frame.data, frame.data + wibeth::kWIBEthFrameBytes);

  wibeth::HeaderPatch patch;
  patch.set_det_id = true;
  patch.det_id = 3;
  patch.set_seq_id = true;
  patch.seq_id = 17;
  patch.force_block_length = true;

  std::vector<std::uint8_t> packet_bytes(wibeth::kEthernetPacketBytes, 0);
  wibeth::construct_packet(packet_bytes.data(), frame.data, golden_config(), patch);

  // Compare the 42-byte header element-wise so a mismatch reports its offset
  // and value.
  const auto expected = expected_header();
  BOOST_CHECK_EQUAL_COLLECTIONS(packet_bytes.begin(),
                                packet_bytes.begin() + expected.size(),
                                expected.begin(),
                                expected.end());

  // Patched DAQ header fields.
  const auto out_header = read_daq_header(packet_bytes.data() + wibeth::kPacketHeaderBytes);
  BOOST_CHECK_EQUAL(out_header.det_id, 3);
  BOOST_CHECK_EQUAL(out_header.crate_id, 1);
  BOOST_CHECK_EQUAL(out_header.slot_id, 1);
  BOOST_CHECK_EQUAL(out_header.stream_id, 0);
  BOOST_CHECK_EQUAL(out_header.seq_id, 17);
  BOOST_CHECK_EQUAL(out_header.block_length, wibeth::kWIBEthBlockLength);
  BOOST_CHECK_EQUAL(out_header.timestamp, 123456789);

  // Complete payload equality outside the patched 16-byte DAQ header.
  BOOST_CHECK_EQUAL(std::memcmp(packet_bytes.data() + wibeth::kPacketHeaderBytes + wibeth::kDAQEthHeaderBytes,
                                input_snapshot.data() + wibeth::kDAQEthHeaderBytes,
                                wibeth::kWIBEthFrameBytes - wibeth::kDAQEthHeaderBytes),
                    0);

  // The input frame must not have been modified.
  BOOST_CHECK_EQUAL(std::memcmp(frame.data, input_snapshot.data(), wibeth::kWIBEthFrameBytes), 0);
}

BOOST_AUTO_TEST_CASE(IPv4ChecksumVerifiedIndependently)
{
  // A second address pair, to check checksum validity beyond the single
  // reference vector.
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0x5a, sizeof(frame.data));

  wibeth::PacketConfig cfg;
  cfg.src_ip = wibeth::parse_ipv4_addr("10.73.139.16");
  cfg.dst_ip = wibeth::parse_ipv4_addr("10.73.139.17");
  cfg.packet_id = 7;

  std::vector<std::uint8_t> packet_bytes(wibeth::kEthernetPacketBytes, 0);
  wibeth::construct_packet(packet_bytes.data(), frame.data, cfg);

  const std::uint8_t* ipv4_header = packet_bytes.data() + wibeth::kEthernetHeaderBytes;
  BOOST_CHECK_EQUAL(ones_complement_sum(ipv4_header, 20), 0xffff);
}

BOOST_AUTO_TEST_CASE(PatchDisabledPreservesFrameVerbatim)
{
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0x77, sizeof(frame.data));

  wibeth::HeaderPatch patch;
  patch.set_det_id = false;
  patch.set_seq_id = false;
  patch.force_block_length = false;

  std::vector<std::uint8_t> packet_bytes(wibeth::kEthernetPacketBytes, 0);
  wibeth::construct_packet(packet_bytes.data(), frame.data, wibeth::PacketConfig{}, patch);

  BOOST_CHECK_EQUAL(
    std::memcmp(packet_bytes.data() + wibeth::kPacketHeaderBytes, frame.data, wibeth::kWIBEthFrameBytes), 0);
}

BOOST_AUTO_TEST_CASE(SequenceIdWrapsModulo4096)
{
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0, sizeof(frame.data));
  wibeth::PacketConfig cfg;

  wibeth::HeaderPatch patch;
  patch.set_seq_id = true;

  std::vector<std::uint8_t> packet_bytes(wibeth::kEthernetPacketBytes, 0);

  patch.seq_id = 4095;
  wibeth::construct_packet(packet_bytes.data(), frame.data, cfg, patch);
  BOOST_CHECK_EQUAL(read_daq_header(packet_bytes.data() + wibeth::kPacketHeaderBytes).seq_id, 4095);

  patch.seq_id = 4096;
  wibeth::construct_packet(packet_bytes.data(), frame.data, cfg, patch);
  BOOST_CHECK_EQUAL(read_daq_header(packet_bytes.data() + wibeth::kPacketHeaderBytes).seq_id, 0);
}

BOOST_AUTO_TEST_CASE(DetIdUpperBoundary63)
{
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0, sizeof(frame.data));
  wibeth::PacketConfig cfg;

  wibeth::HeaderPatch patch;
  patch.set_det_id = true;
  patch.det_id = 63; // largest value the 6-bit field can carry

  std::vector<std::uint8_t> packet_bytes(wibeth::kEthernetPacketBytes, 0);
  wibeth::construct_packet(packet_bytes.data(), frame.data, cfg, patch);
  BOOST_CHECK_EQUAL(read_daq_header(packet_bytes.data() + wibeth::kPacketHeaderBytes).det_id, 63);
}

BOOST_AUTO_TEST_CASE(DetIdTruncatesToSixBits)
{
  // The patch stores det_id in the 6-bit DAQEthHeader field, so 64 wraps to 0.
  // The plugin rejects det_id > 63 at configuration.  Direct users of the
  // builder get this truncation.
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0, sizeof(frame.data));
  wibeth::PacketConfig cfg;

  wibeth::HeaderPatch patch;
  patch.set_det_id = true;
  patch.det_id = 64;

  std::vector<std::uint8_t> packet_bytes(wibeth::kEthernetPacketBytes, 0);
  wibeth::construct_packet(packet_bytes.data(), frame.data, cfg, patch);
  BOOST_CHECK_EQUAL(read_daq_header(packet_bytes.data() + wibeth::kPacketHeaderBytes).det_id, 0);
}

BOOST_AUTO_TEST_CASE(ConstructsIntoMisalignedDestination)
{
  dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter frame{};
  std::memset(frame.data, 0xcd, sizeof(frame.data));

  wibeth::HeaderPatch patch;
  patch.set_det_id = true;
  patch.det_id = 5;
  patch.set_seq_id = true;
  patch.seq_id = 9;

  // Offset the destination by one byte so no field is naturally aligned.
  // Construction reaches caller storage only through memcpy, so the result must
  // be byte-identical to an aligned construction.
  std::vector<std::uint8_t> aligned(wibeth::kEthernetPacketBytes, 0);
  wibeth::construct_packet(aligned.data(), frame.data, golden_config(), patch);

  std::vector<std::uint8_t> raw(wibeth::kEthernetPacketBytes + 1, 0);
  std::uint8_t* dst = raw.data() + 1;
  wibeth::construct_packet(dst, frame.data, golden_config(), patch);

  BOOST_CHECK_EQUAL(std::memcmp(dst, aligned.data(), wibeth::kEthernetPacketBytes), 0);
  BOOST_CHECK_EQUAL(raw.back(), 0xcd);

  const auto out_header = read_daq_header(dst + wibeth::kPacketHeaderBytes);
  BOOST_CHECK_EQUAL(out_header.det_id, 5);
  BOOST_CHECK_EQUAL(out_header.seq_id, 9);
  BOOST_CHECK_EQUAL(out_header.block_length, wibeth::kWIBEthBlockLength);
}

BOOST_AUTO_TEST_SUITE_END()
