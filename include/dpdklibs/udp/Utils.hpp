/**
 * @file Utils.hpp Utility functions for UDP packets and payloads
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_SRC_UTILS_HPP_
#define DPDKLIBS_SRC_UTILS_HPP_

#include "IPV4UDPPacket.hpp"

#include "detdataformats/DAQEthHeader.hpp"
#include "logging/Logging.hpp"

#include "fmt/core.h"
#include "rte_ether.h"
#include "rte_mbuf.h"

#include <cstdint>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dunedaq {

ERS_DECLARE_ISSUE(dpdklibs, 
  BadPacketHeaderIssue, 
  "BadPacketHeaderIssue: \"" << ers_messg << "\"",
  ((std::string)ers_messg))

namespace dpdklibs {
namespace udp {

uint16_t
get_payload_size_udp_hdr(struct rte_udp_hdr* udp_hdr);
uint16_t
get_payload_size(struct ipv4_udp_packet_hdr* ipv4_udp_hdr);

rte_be32_t
ip_address_dotdecimal_to_binary(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4);
struct ipaddr
ip_address_binary_to_dotdecimal(rte_le32_t binary_ipv4_address);

std::string
get_ipv4_decimal_addr_str(struct ipaddr ipv4_address);

char*
get_udp_payload(const rte_mbuf* mbuf);

// void dump_udp_header(struct ipv4_udp_packet_hdr * pkt);
std::string
get_udp_header_str(struct rte_mbuf* mbuf);

std::string
get_udp_packet_str(struct rte_mbuf* mbuf);

void
add_file_contents_to_vector(const std::string& filename, std::vector<char>& buffervec);

std::vector<std::pair<const void*, int>>
get_ethernet_packets(const std::vector<char>& buffervec);

void
set_daqethheader_test_values(detdataformats::DAQEthHeader& daqethheader_obj) noexcept;

std::string
get_rte_mbuf_str(const rte_mbuf* mbuf) noexcept;

struct StreamUID
{
  uint64_t det_id : 6;
  uint64_t crate_id : 10;
  uint64_t slot_id : 4;
  uint64_t stream_id : 8;

  StreamUID() = default;

  StreamUID(const detdataformats::DAQEthHeader& daq_hdr)
    : det_id(daq_hdr.det_id)
    , crate_id(daq_hdr.crate_id)
    , slot_id(daq_hdr.slot_id)
    , stream_id(daq_hdr.stream_id){};

  bool operator<(const StreamUID& rhs) const { return std::tie(det_id, crate_id, slot_id, stream_id) < std::tie(rhs.det_id, rhs.crate_id, rhs.slot_id, rhs.stream_id); }

  bool operator==(const StreamUID& rhs) const { return det_id == rhs.det_id && crate_id == rhs.crate_id && slot_id == rhs.slot_id && stream_id == rhs.stream_id; }
  operator std::string() const { return fmt::format("({}, {}, {}, {})", det_id, crate_id, slot_id, stream_id); }
};

// Note that there's a difference between the string representation
// we'd want for a stream as an opmon label and the kind we'd want
// to print to screen. Thus this function...

std::string
get_opmon_string(const StreamUID& sid);

struct ReceiverStats
{

  std::atomic<int64_t> total_packets = 0;
  std::atomic<int64_t> min_packet_size = std::numeric_limits<int64_t>::max();
  std::atomic<int64_t> max_packet_size = std::numeric_limits<int64_t>::min();
  std::atomic<int64_t> max_timestamp_deviation = 0; // In absolute terms (positive *or* negative)
  std::atomic<int64_t> max_seq_id_deviation = 0;    // " " "

  std::atomic<int64_t> packets_since_last_reset = 0;
  std::atomic<int64_t> bytes_since_last_reset = 0;
  std::atomic<int64_t> bad_timestamps_since_last_reset = 0;
  std::atomic<int64_t> bad_sizes_since_last_reset = 0;
  std::atomic<int64_t> bad_seq_ids_since_last_reset = 0;

  ReceiverStats() = default;
  ReceiverStats(const ReceiverStats& rhs);
  ReceiverStats& operator=(const ReceiverStats& rhs);

  // If you've only been collecting stats for one out of every N
  // packets, you'd want to pass N to "scale" to correct for this

  void scale(int64_t sf)
  {
    packets_since_last_reset = packets_since_last_reset.load() * sf;
    bytes_since_last_reset = bytes_since_last_reset.load() * sf;
    bad_timestamps_since_last_reset = bad_timestamps_since_last_reset.load() * sf;
    bad_sizes_since_last_reset = bad_sizes_since_last_reset.load() * sf;
    bad_seq_ids_since_last_reset = bad_seq_ids_since_last_reset.load() * sf;
  }

  void reset()
  {
    packets_since_last_reset = 0;
    bytes_since_last_reset = 0;
    bad_timestamps_since_last_reset = 0;
    bad_sizes_since_last_reset = 0;
    bad_seq_ids_since_last_reset = 0;
    max_timestamp_deviation = 0;
    max_seq_id_deviation = 0;
  }

  void merge(const std::vector<ReceiverStats>& stats_vector);

  operator std::string() const;
};

// Derive quantities (e.g., bytes/s) from a ReceiverStats object and store it in a jsonnet-based struct
receiverinfo::Info
DeriveFromReceiverStats(const ReceiverStats& receiver_stats, double time_per_report);

// This class will, on a per-stream basis, fill in ReceiverStats
// instances given packets. In the constructor you can tell it
// whether you want it to pay attention to a packet's sequence ID
// and/or timestamp and/or size.

class PacketInfoAccumulator
{

public:
  static constexpr int64_t s_ignorable_value = std::numeric_limits<int64_t>::max();
  static constexpr int64_t s_max_seq_id = 4095;

  PacketInfoAccumulator(int64_t expected_seq_id_step = s_ignorable_value, int64_t expected_timestamp_step = s_ignorable_value, int64_t expected_size = s_ignorable_value, int64_t process_nth_packet = 1);

  void process_packet(const detdataformats::DAQEthHeader& daq_hdr, const int64_t data_len);
  void reset();
  void dump();

  // get_and_reset_stream_stats() will take each per-stream statistic
  // and reset the values used to calculate rates (packets per
  // second, etc.)

  std::map<StreamUID, ReceiverStats> get_and_reset_stream_stats();

  // erase_stream_stats() is what you'd call between runs, so, e.g., your total packet count doesn't persist
  void erase_stream_stats();

  void set_expected_packet_size(int64_t packet_size) { m_expected_size = packet_size; }

  PacketInfoAccumulator(const PacketInfoAccumulator&) = delete;
  PacketInfoAccumulator& operator=(const PacketInfoAccumulator&) = delete;
  PacketInfoAccumulator(PacketInfoAccumulator&&) = delete;
  PacketInfoAccumulator& operator=(PacketInfoAccumulator&&) = delete;

  ~PacketInfoAccumulator() = default;

private:
  std::map<StreamUID, ReceiverStats> m_stream_stats_atomic;

  const int64_t m_expected_seq_id_step;

  const int64_t m_expected_timestamp_step;
  int64_t m_expected_size;
  const int64_t m_process_nth_packet;

  int64_t m_next_expected_seq_id[s_max_seq_id + 1];

  std::map<StreamUID, int64_t> m_stream_last_timestamp;
  std::map<StreamUID, int64_t> m_stream_last_seq_id;
};
} // namespace udp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_SRC_UTILS_HPP_
