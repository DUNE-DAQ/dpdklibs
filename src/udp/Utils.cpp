/**
 * @file Utils.cpp UDP related utility functions' implementations
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "dpdklibs/udp/Utils.hpp"

#include "detdataformats/DAQEthHeader.hpp"
#include "logging/Logging.hpp"
#include "readoutlibs/ReadoutIssues.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dunedaq {
namespace dpdklibs {
namespace udp {

std::uint16_t
get_payload_size_udp_hdr(struct rte_udp_hdr* udp_hdr)
{
  return rte_be_to_cpu_16(udp_hdr->dgram_len) - sizeof(struct rte_udp_hdr);
}

std::uint16_t
get_payload_size(struct ipv4_udp_packet_hdr* ipv4_udp_hdr)
{
  return rte_be_to_cpu_16(ipv4_udp_hdr->udp_hdr.dgram_len) - sizeof(struct rte_udp_hdr);
}

rte_be32_t
ip_address_dotdecimal_to_binary(std::uint8_t byte1, std::uint8_t byte2, std::uint8_t byte3, std::uint8_t byte4)
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
get_ipv4_decimal_addr_str(struct ipaddr ipv4_address)
{
  std::ostringstream ostrs;
  ostrs << (unsigned)ipv4_address.addr_bytes[3] << '.' << (unsigned)ipv4_address.addr_bytes[2] << '.' << (unsigned)ipv4_address.addr_bytes[1] << '.' << (unsigned)ipv4_address.addr_bytes[0];
  return ostrs.str();
  /*printf("%i.%i.%i.%i",
        ipv4_address.addr_bytes[3],
        ipv4_address.addr_bytes[2],
        ipv4_address.addr_bytes[1],
        ipv4_address.addr_bytes[0]);
  */
}

char*
get_udp_payload(const rte_mbuf* mbuf)
{
  struct ipv4_udp_packet_hdr* udp_packet = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr*);
  // dump_udp_header(udp_packet);
  // uint16_t payload_size = get_payload_size(udp_packet);
  char* payload = (char*)(udp_packet + 1);
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
hex_digits_to_stream(std::ostringstream& ostrs, int value, char separator = ':', char fill = '0', int digits = 2)
{
  ostrs << std::setfill(fill) << std::setw(digits) << std::hex << value << std::dec << separator;
}

std::string
// dump_udp_header(struct ipv4_udp_packet_hdr * pkt)
get_udp_header_str(struct rte_mbuf* mbuf)
{
  struct ipv4_udp_packet_hdr* pkt = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr*);
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
  std::string srcaddr = get_ipv4_decimal_addr_str(ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.src_addr)));
  std::string dstaddr = get_ipv4_decimal_addr_str(ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(pkt->ipv4_hdr.dst_addr)));
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
get_udp_packet_str(struct rte_mbuf* mbuf)
{
  struct ipv4_udp_packet_hdr* pkt = rte_pktmbuf_mtod(mbuf, struct ipv4_udp_packet_hdr*);
  char* payload = (char*)(pkt);
  std::ostringstream ostrs;
  std::uint8_t byte;
  for (byte = 0; byte < rte_be_to_cpu_16(pkt->udp_hdr.dgram_len); byte++) {
    hex_digits_to_stream(ostrs, (unsigned)(*(payload + byte)), ' ');
    // printf("%02x ", *(payload + byte) & 0xFF);
    // printf("%s", (payload + byte));
  }
  return ostrs.str();
}

void
add_file_contents_to_vector(const std::string& filename, std::vector<char>& buffervec)
{

  char byte = 0x0;

  std::ifstream packetfile;
  packetfile.open(filename, std::ios::binary);

  if (!packetfile) {
    throw ::dunedaq::readoutlibs::CannotOpenFile(ERS_HERE, filename);
  }

  while (packetfile.get(byte)) {
    buffervec.push_back(byte);
  }

  packetfile.close();
}

std::vector<std::pair<const void*, int>>
get_ethernet_packets(const std::vector<char>& buffervec)
{

  std::vector<std::pair<const void*, int>> ethernet_packets;
  const std::vector<uint16_t> allowed_ethertypes{ 0x0800, 0x0806 };

  for (int byte_index = 0; byte_index < buffervec.size();) {
    const auto buf_ptr = &buffervec.at(byte_index);
    auto hdr = reinterpret_cast<const ipv4_udp_packet_hdr*>(buf_ptr);

    // A sanity check
    bool match = false;
    for (auto allowed_ethertype : allowed_ethertypes) {
      if (hdr->eth_hdr.ether_type == rte_be_to_cpu_16(allowed_ethertype)) {
        match = true;
        break;
      }
    }

    if (!match) {
      std::stringstream msgstr;
      msgstr << "Ether type in ethernet header (value " << std::hex << rte_be_to_cpu_16(hdr->eth_hdr.ether_type) << std::dec << ") either unknown or unsupported";
      throw dunedaq::dpdklibs::BadPacketHeaderIssue(ERS_HERE, msgstr.str());
    }

    int ipv4_packet_size = rte_be_to_cpu_16(hdr->ipv4_hdr.total_length);
    constexpr int min_packet_size = sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr);
    constexpr int max_packet_size = 10000;

    if (ipv4_packet_size < min_packet_size || ipv4_packet_size > max_packet_size) {
      std::stringstream msgstr;
      msgstr << "Calculated IPv4 packet size of " << ipv4_packet_size << " bytes is out of the required range of (" << min_packet_size << ", " << max_packet_size << ") bytes";
      throw dunedaq::dpdklibs::BadPacketHeaderIssue(ERS_HERE, msgstr.str());
    }

    int ethernet_packet_size = sizeof(rte_ether_hdr) + ipv4_packet_size;
    ethernet_packets.emplace_back(std::pair<const void*, int>{ buf_ptr, ethernet_packet_size });
    byte_index += ethernet_packet_size;
  }

  return ethernet_packets;
}

void
set_daqethheader_test_values(detdataformats::DAQEthHeader& daqethheader_obj) noexcept
{
  daqethheader_obj.version = 0;
  daqethheader_obj.det_id = 1;
  daqethheader_obj.crate_id = 2;
  daqethheader_obj.slot_id = 3;
  daqethheader_obj.stream_id = 4;
  daqethheader_obj.reserved = 5;
  daqethheader_obj.seq_id = 6;
  daqethheader_obj.block_length = 7;
  daqethheader_obj.timestamp = 8;
}

std::string
get_rte_mbuf_str(const rte_mbuf* mbuf) noexcept
{
  std::stringstream ss;

  ss << "\nrte_mbuf info:";
  ss << "\npkt_len: " << mbuf->pkt_len;
  ss << "\ndata_len: " << mbuf->data_len;
  ss << "\nBuffer address: " << std::hex << mbuf->buf_addr;
  ss << "\nRef count: " << std::dec << rte_mbuf_refcnt_read(mbuf);
  ss << "\nport: " << mbuf->port;
  ss << "\nol_flags: " << std::hex << mbuf->ol_flags;
  ss << "\npacket_type: " << std::dec << mbuf->packet_type;
  ss << "\nl2 type: " << static_cast<int>(mbuf->l2_type);
  ss << "\nl3 type: " << static_cast<int>(mbuf->l3_type);
  ss << "\nl4 type: " << static_cast<int>(mbuf->l4_type);
  ss << "\ntunnel type: " << static_cast<int>(mbuf->tun_type);
  ss << "\nInner l2 type: " << static_cast<int>(mbuf->inner_l2_type);
  ss << "\nInner l3 type: " << static_cast<int>(mbuf->inner_l3_type);
  ss << "\nInner l4 type: " << static_cast<int>(mbuf->inner_l4_type);
  ss << "\nbuf_len: " << mbuf->buf_len;
  ss << "\nl2_len: " << mbuf->l2_len;
  ss << "\nl3_len: " << mbuf->l3_len;
  ss << "\nl4_len: " << mbuf->l4_len;
  ss << "\nouter_l2_len: " << mbuf->outer_l2_len;
  ss << "\nouter_l3_len: " << mbuf->outer_l3_len;
  ss << std::dec;

  return ss.str();
}

PacketInfoAccumulator::PacketInfoAccumulator(int64_t expected_seq_id_step, int64_t expected_timestamp_step, int64_t expected_size, int64_t process_nth_packet)
  : m_expected_seq_id_step(expected_seq_id_step)
  , m_expected_timestamp_step(expected_timestamp_step)
  , m_expected_size(expected_size)
  , m_process_nth_packet(process_nth_packet)
{
  // To be clear, the reason you'd set "expected_seq_id_step" to
  // anything other than 1 or PacketInfoAccumulator::s_ignorable_value
  // is to test that the code correctly handles unexpected sequence
  // IDs

  if (expected_seq_id_step != PacketInfoAccumulator::s_ignorable_value) {

    for (int i = 0; i <= s_max_seq_id; ++i) {
      m_next_expected_seq_id[i] = i + expected_seq_id_step;
      if (m_next_expected_seq_id[i] > s_max_seq_id) {
        m_next_expected_seq_id[i] -= (s_max_seq_id + 1);
      }
    }
  }
}

void
PacketInfoAccumulator::process_packet(const detdataformats::DAQEthHeader& daq_hdr, const int64_t data_len)
{

  StreamUID unique_str_id(daq_hdr);
  bool first_packet_in_stream = false;

  // n.b. C++ knows to add the unique_str_id as a key and a default-constructed ReceiverStats as a value if it's not already in the map
  ReceiverStats& receiver_stats{ m_stream_stats_atomic[unique_str_id] };

  if (receiver_stats.total_packets % m_process_nth_packet != 0) {
    receiver_stats.total_packets++;
    m_stream_last_seq_id[unique_str_id] = static_cast<int64_t>(daq_hdr.seq_id);
    m_stream_last_timestamp[unique_str_id] = static_cast<int64_t>(daq_hdr.timestamp);
    return;
  }

  if (receiver_stats.total_packets == 0) {

    first_packet_in_stream = true;
    m_stream_last_seq_id[unique_str_id] = static_cast<int64_t>(daq_hdr.seq_id);
    m_stream_last_timestamp[unique_str_id] = static_cast<int64_t>(daq_hdr.timestamp);

    // TLOG() << "Found first packet in " << static_cast<std::string>(unique_str_id);
  }

  receiver_stats.total_packets++;
  receiver_stats.packets_since_last_reset++;

  receiver_stats.bytes_since_last_reset += data_len;

  if (data_len > receiver_stats.max_packet_size) {
    receiver_stats.max_packet_size = data_len;
  }

  if (data_len < receiver_stats.min_packet_size) {
    receiver_stats.min_packet_size = data_len;
  }

  if (m_expected_size != s_ignorable_value && data_len != m_expected_size) {
    receiver_stats.bad_sizes_since_last_reset++;
  }

  if (m_expected_seq_id_step != s_ignorable_value && !first_packet_in_stream) {

    // seq_id is represented in an unsigned 64-bit int in
    // DAQEthHeader, so if we don't convert it to a *signed* int
    // before doing arithmetic, bad things will happen.

    auto seq_id = static_cast<int64_t>(daq_hdr.seq_id);
    auto& last_seq_id = m_stream_last_seq_id[unique_str_id];

    if (seq_id != m_next_expected_seq_id[last_seq_id]) {
      receiver_stats.bad_seq_ids_since_last_reset++;

      int64_t seq_id_delta = seq_id - m_next_expected_seq_id[last_seq_id];

      if (seq_id_delta < 0) { // e.g., we expected seq ID 4095 but got 0 instead
        seq_id_delta += PacketInfoAccumulator::s_max_seq_id + 1;
      }

      if (seq_id_delta > receiver_stats.max_seq_id_deviation.load()) {
        receiver_stats.max_seq_id_deviation = seq_id_delta;
      }
    }

    last_seq_id = daq_hdr.seq_id;
  }

  if (m_expected_timestamp_step != s_ignorable_value && !first_packet_in_stream) {

    auto timestamp = static_cast<int64_t>(daq_hdr.timestamp);
    auto& last_timestamp = m_stream_last_timestamp[unique_str_id];

    if (timestamp != last_timestamp + m_expected_timestamp_step) {

      int64_t timestamp_delta = daq_hdr.timestamp - (last_timestamp + m_expected_timestamp_step);
      receiver_stats.bad_timestamps_since_last_reset++;

      if (timestamp_delta > receiver_stats.max_timestamp_deviation.load()) {
        receiver_stats.max_timestamp_deviation = timestamp_delta;
      }
    }

    last_timestamp = timestamp;
  }
}

// dump() is more a function to test the development of
// PacketInfoAccumulator itself than a function users of
// PacketInfoAccumulator would call

void
PacketInfoAccumulator::dump()
{

  for (auto& stream_stat : m_stream_stats_atomic) {
    std::stringstream info;

    auto& streamid = stream_stat.first;
    auto& stats = stream_stat.second;

    TLOG() << static_cast<std::string>(streamid) + "\n" + static_cast<std::string>(stats);
  }
}

void
PacketInfoAccumulator::erase_stream_stats()
{

  m_stream_stats_atomic.clear();
  m_stream_last_seq_id.clear();
}

std::map<StreamUID, ReceiverStats>
PacketInfoAccumulator::get_and_reset_stream_stats()
{

  auto snapshot_before_reset = m_stream_stats_atomic;

  for (auto& stream_stat : m_stream_stats_atomic) {
    stream_stat.second.reset();
  }

  if (m_process_nth_packet != 1) {
    for (auto& stream_stat : snapshot_before_reset) {
      stream_stat.second.scale(m_process_nth_packet);
    }
  }

  return snapshot_before_reset;
}

ReceiverStats::ReceiverStats(const ReceiverStats& rhs)
  : total_packets(rhs.total_packets.load())
  , min_packet_size(rhs.min_packet_size.load())
  , max_packet_size(rhs.max_packet_size.load())
  , max_timestamp_deviation(rhs.max_timestamp_deviation.load())
  , max_seq_id_deviation(rhs.max_seq_id_deviation.load())
  , packets_since_last_reset(rhs.packets_since_last_reset.load())
  , bytes_since_last_reset(rhs.bytes_since_last_reset.load())
  , bad_timestamps_since_last_reset(rhs.bad_timestamps_since_last_reset.load())
  , bad_sizes_since_last_reset(rhs.bad_sizes_since_last_reset.load())
  , bad_seq_ids_since_last_reset(rhs.bad_seq_ids_since_last_reset.load())
{
}

ReceiverStats&
ReceiverStats::operator=(const ReceiverStats& rhs)
{

  total_packets = rhs.total_packets.load();
  min_packet_size = rhs.min_packet_size.load();
  max_packet_size = rhs.max_packet_size.load();
  max_timestamp_deviation = rhs.max_timestamp_deviation.load();
  max_seq_id_deviation = rhs.max_seq_id_deviation.load();
  packets_since_last_reset = rhs.packets_since_last_reset.load();
  bytes_since_last_reset = rhs.bytes_since_last_reset.load();
  bad_timestamps_since_last_reset = rhs.bad_timestamps_since_last_reset.load();
  bad_sizes_since_last_reset = rhs.bad_sizes_since_last_reset.load();
  bad_seq_ids_since_last_reset = rhs.bad_seq_ids_since_last_reset.load();

  return *this;
}

ReceiverStats::operator std::string() const
{

  std::stringstream reportstr;

  reportstr << "total_packets == " << total_packets << "\n"
            << "min_packet_size == " << min_packet_size << "\n"
            << "max_packet_size == " << max_packet_size << "\n"
            << "max_timestamp_deviation == " << max_timestamp_deviation << "\n"
            << "max_seq_id_deviation == " << max_seq_id_deviation << "\n"
            << "packets_since_last_reset == " << packets_since_last_reset << "\n"
            << "bytes_since_last_reset == " << bytes_since_last_reset << "\n"
            << "bad_timestamps_since_last_reset == " << bad_timestamps_since_last_reset << "\n"
            << "bad_sizes_since_last_reset == " << bad_sizes_since_last_reset << "\n"
            << "bad_seq_ids_since_last_reset == " << bad_seq_ids_since_last_reset << "\n";

  return reportstr.str();
}

void
ReceiverStats::merge(const std::vector<ReceiverStats>& stats_vector)
{

  for (auto& stats : stats_vector) {
    total_packets += stats.total_packets.load();
    min_packet_size = min_packet_size.load() < stats.min_packet_size.load() ? min_packet_size.load() : stats.min_packet_size.load();
    max_packet_size = max_packet_size.load() > stats.max_packet_size.load() ? max_packet_size.load() : stats.max_packet_size.load();
    max_seq_id_deviation = max_seq_id_deviation.load() > stats.max_seq_id_deviation.load() ? max_seq_id_deviation.load() : stats.max_seq_id_deviation.load();
    packets_since_last_reset += stats.packets_since_last_reset.load();
    bytes_since_last_reset += stats.bytes_since_last_reset.load();
    bad_sizes_since_last_reset += stats.bad_sizes_since_last_reset.load();
    bad_seq_ids_since_last_reset += stats.bad_seq_ids_since_last_reset.load();
  }
}

receiverinfo::Info
DeriveFromReceiverStats(const ReceiverStats& receiver_stats, double time_per_report)
{

  receiverinfo::Info derived_stats;

  derived_stats.total_packets = receiver_stats.total_packets.load();
  derived_stats.packets_per_second = receiver_stats.packets_since_last_reset.load() / time_per_report;
  derived_stats.bytes_per_second = receiver_stats.bytes_since_last_reset.load() / time_per_report;
  derived_stats.bad_seq_id_packets_per_second = receiver_stats.bad_seq_ids_since_last_reset.load() / time_per_report;
  derived_stats.max_bad_seq_id_deviation = receiver_stats.max_seq_id_deviation.load();
  derived_stats.bad_size_packets_per_second = receiver_stats.bad_sizes_since_last_reset.load() / time_per_report;
  derived_stats.max_packet_size = receiver_stats.max_packet_size.load();
  derived_stats.min_packet_size = receiver_stats.min_packet_size.load();

  return derived_stats;
}

std::string
get_opmon_string(const StreamUID& sid)
{
  std::stringstream opmonstr;
  opmonstr << "det" << sid.det_id << "_crt" << sid.crate_id << "_slt" << sid.slot_id << "_str" << sid.stream_id;
  return opmonstr.str();
}

} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq
