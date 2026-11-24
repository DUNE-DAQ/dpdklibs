/**
 * @file FakeSenderUtils.hpp
 *
 * Pure helper utilities for DPDKFakeDataSender, extracted into an internal
 * namespace so they are unit-testable without EAL, a NIC, or an OKS database.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_FAKESENDERUTILS_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_FAKESENDERUTILS_HPP_

#include "logging/Logging.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include "dpdklibs/udp/PacketCtor.hpp"

#include <sys/types.h>

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace dunedaq::dpdklibs::sender::detail {

// DPDK type of port IDs, queue IDs, queue counts and ring sizes (16 bits).
using dpdk_id_t = decltype(rte_eth_dev_count_avail());

inline std::vector<std::string>
split_args(const std::string& args)
{
  std::istringstream input(args);
  std::vector<std::string> output;
  std::string token;
  while (input >> token) {
    output.push_back(token);
  }
  return output;
}

inline std::vector<std::string>
build_eal_args(const std::string& tx_pcie,
               const std::string& eal_lcores,
               const std::string& extra_eal_args,
               pid_t pid)
{
  std::vector<std::string> eal_args{
    "dpdklibs_dpdk_fake_data_sender",
    "--proc-type=primary",
    "-a",
    tx_pcie,
    "-l",
    eal_lcores,
    "--file-prefix",
    std::string("dpdklibs_fake_sender_") + std::to_string(pid)
  };
  const auto extra_args = split_args(extra_eal_args);
  eal_args.insert(eal_args.end(), extra_args.begin(), extra_args.end());
  return eal_args;
}

// Convert value to dpdk_id_t.  Throws std::runtime_error if negative or > uint16_t max.
template<typename T>
dpdk_id_t
checked_id(T value, const std::string& name)
{
  if constexpr (std::is_signed_v<T>) {
    if (value < 0) {
      throw std::runtime_error(name + " does not fit into uint16_t");
    }
  }
  if (static_cast<std::uint64_t>(value) > std::numeric_limits<dpdk_id_t>::max()) {
    throw std::runtime_error(name + " does not fit into uint16_t");
  }
  return static_cast<dpdk_id_t>(value);
}

inline rte_ether_addr
parse_mac(const std::string& mac)
{
  rte_ether_addr addr{};
  if (!dunedaq::dpdklibs::udp::get_ether_addr6(mac.c_str(), &addr)) {
    throw std::runtime_error("Invalid MAC address: " + mac);
  }
  return addr;
}

inline std::string
format_mac(const rte_ether_addr& addr)
{
  std::ostringstream os;
  os << std::hex << std::setfill('0');
  const char* separator = "";
  for (const auto byte : addr.addr_bytes) {
    os << separator << std::setw(2) << static_cast<int>(byte);
    separator = ":";
  }
  return os.str();
}

struct StreamAddress
{
  std::string src_ip;
  std::int64_t det_id;
};

// Source IP and detector ID of one stream, from objects the receiver reads.
// Source IP: first ip_address of the NWDetDataSender that lists the stream's source_id.
// Detector ID: detector_id of the stream's GeoId.
template<typename ConnectionT, typename CallbackT>
StreamAddress
stream_address(const ConnectionT* connection, const CallbackT* callback)
{
  const auto source_id = callback->get_source_id();
  for (const auto* sender : connection->get_net_senders()) {
    for (const auto* stream : sender->get_streams()) {
      if (stream->get_source_id() == source_id) {
        const auto& addresses = sender->get_uses()->get_ip_address();
        if (addresses.empty()) {
          throw dunedaq::datahandlinglibs::InitializationError(
            ERS_HERE, "NetworkInterface " + sender->get_uses()->UID() + " has no ip_address");
        }
        return { addresses.front(), static_cast<std::int64_t>(stream->get_geo_id()->get_detector_id()) };
      }
    }
  }
  throw dunedaq::datahandlinglibs::InitializationError(ERS_HERE,
                                                       "source_id " + std::to_string(source_id) +
                                                         " is not a stream of any sender in connection " +
                                                         connection->UID());
}

} // namespace dunedaq::dpdklibs::sender::detail

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_FAKESENDERUTILS_HPP_
