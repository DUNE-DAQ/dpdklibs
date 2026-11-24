/**
 * @file FakeSenderCore.hpp
 *
 * Orchestration core for DPDKFakeDataSender: DAL/config loading, pre-EAL
 * validation, port discovery and queue/mempool bring-up, FSM gating, and
 * idempotent teardown/rollback.  Templated on the DPDK Runtime seam, TX
 * Backend, PayloadT, and CallbackConfT so every code path is unit-testable
 * without EAL, a NIC, hugepages, or an OKS database.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_FAKESENDERCORE_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_FAKESENDERCORE_HPP_

#include "logging/Logging.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "dpdklibs/sender/FakeSenderUtils.hpp"
#include "dpdklibs/sender/WIBEthTransmitter.hpp"
#include "dpdklibs/wibeth/WIBEthPacketBuilder.hpp"

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dunedaq::dpdklibs::sender {

struct FakeSenderConfig
{
  std::string eal_args;
  std::string eal_lcores;
  std::string tx_pcie;
  std::uint16_t src_port = 55677;
  std::uint16_t dst_port = 55678;
  std::uint32_t initial_seq_id = 0;
  bool defer_tx_until_enable_triggers = false;
  std::uint32_t tx_ring_size = 1024;
  std::uint32_t num_bufs = 2047;
  std::uint32_t mbuf_cache_size = 250;
  std::uint32_t data_room_size = 9800;
};

namespace detail {

// Pure pre-EAL configuration checks.  Separated so boundary values such as
// tx_queue_count > uint16_t::max can also be tested directly without
// allocating 65k+ transmitter instances.
inline void
validate_sender_config(const FakeSenderConfig& conf,
                       const std::string& dst_mac_str,
                       const std::string& dst_ip,
                       const std::vector<std::string>& stream_src_ips,
                       const std::vector<std::int64_t>& stream_det_ids,
                       const std::vector<std::uint32_t>& stream_source_ids,
                       std::size_t tx_queue_count)
{
  // The mbuf data room must hold headroom plus one complete WIBEth packet, and
  // must survive the uint16_t data-room parameter of mempool creation without
  // truncation.
  const std::int64_t required_room =
    static_cast<std::int64_t>(RTE_PKTMBUF_HEADROOM) + static_cast<std::int64_t>(wibeth::kEthernetPacketBytes);
  const std::int64_t data_room = conf.data_room_size;
  if (data_room < required_room) {
    throw datahandlinglibs::InitializationError(
      ERS_HERE,
      "data_room_size " + std::to_string(data_room) + " is smaller than required " + std::to_string(required_room) +
        " (RTE_PKTMBUF_HEADROOM + complete WIBEth packet)");
  }
  if (data_room > std::numeric_limits<dpdk_id_t>::max()) {
    throw datahandlinglibs::InitializationError(
      ERS_HERE, "data_room_size " + std::to_string(data_room) + " does not fit the uint16_t mbuf data room");
  }

  // det_id is patched into the 6-bit DAQEthHeader field; anything outside
  // 0..63 would silently truncate on the wire (64 becomes 0).
  for (std::size_t q = 0; q < stream_det_ids.size(); ++q) {
    if (stream_det_ids[q] < 0 || stream_det_ids[q] > 63) {
      throw datahandlinglibs::InitializationError(
        ERS_HERE,
        "detector_id " + std::to_string(stream_det_ids[q]) + " of source_id " +
          std::to_string(stream_source_ids[q]) + " exceeds the 6-bit DAQEthHeader range (0..63)");
    }
  }

  // The schema default of NetworkInterface.mac_address is all zero: the
  // receiver interface is not configured.
  const auto dst_mac = parse_mac(dst_mac_str);
  if (rte_is_zero_ether_addr(&dst_mac)) {
    throw datahandlinglibs::InitializationError(ERS_HERE,
                                                "The net_receiver interface of the connection has no mac_address");
  }

  // Validate destination and per-stream source IPv4 addresses before EAL/NIC
  // side effects.
  wibeth::parse_ipv4_addr(dst_ip);
  for (const auto& src_ip : stream_src_ips) {
    wibeth::parse_ipv4_addr(src_ip);
  }

  // A TX mempool not larger than its TX ring may stall the stream.  Observed
  // with 4095 mbufs and 4096 descriptors: tx_alloc_failures on ~50% of frames.

  // Ring sizes and queue counts are validated here (pre-EAL) instead of first
  // failing deep inside interface setup.
  checked_id(conf.tx_ring_size, "tx_ring_size");
  checked_id(tx_queue_count, "TX queue count");
}

} // namespace detail

template<typename Runtime, typename Backend, typename PayloadT, typename CallbackConfT>
class FakeSenderCore
{
public:
  using transmitter_t = WIBEthTransmitter<Backend>;
  using dpdk_id_t = detail::dpdk_id_t;
  using pool_handle_t = typename Runtime::pool_handle_t;

  explicit FakeSenderCore(std::string name, Runtime runtime = Runtime{})
    : m_name(std::move(name))
    , m_runtime(std::move(runtime))
  {
  }

  ~FakeSenderCore() { close_dpdk(); }

  FakeSenderCore(const FakeSenderCore&) = delete;
  FakeSenderCore& operator=(const FakeSenderCore&) = delete;
  FakeSenderCore(FakeSenderCore&&) = delete;
  FakeSenderCore& operator=(FakeSenderCore&&) = delete;

  template<typename ModuleDalT, typename ConfigManagerT>
  void init(ConfigManagerT& cfg)
  {
    const auto* mdal = cfg.template get_dal<ModuleDalT>(m_name);
    if (mdal == nullptr) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "No DPDKFakeDataSender DAL object found for " + m_name);
    }

    const auto* conf = mdal->get_configuration();
    if (conf == nullptr) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "No DPDKFakeDataSenderConf object found for " + m_name);
    }

    const auto* connection = mdal->get_connection();
    if (connection == nullptr || connection->get_net_receiver() == nullptr ||
        connection->get_net_receiver()->get_uses() == nullptr) {
      throw datahandlinglibs::InitializationError(ERS_HERE,
                                                  "No net_receiver NetworkInterface configured for " + m_name);
    }
    const auto* rx_interface = connection->get_net_receiver()->get_uses();
    if (rx_interface->get_ip_address().empty()) {
      throw datahandlinglibs::InitializationError(ERS_HERE,
                                                  "NetworkInterface " + rx_interface->UID() + " has no ip_address");
    }

    const auto callbacks = mdal->get_callbacks();
    if (callbacks.empty()) {
      throw datahandlinglibs::InitializationError(ERS_HERE,
                                                  "DPDKFakeDataSender requires at least one DataMoveCallbackConf");
    }

    FakeSenderConfig parsed_conf;
    parsed_conf.eal_args = conf->get_eal_args();
    parsed_conf.eal_lcores = conf->get_eal_lcores();
    parsed_conf.tx_pcie = conf->get_tx_pcie();
    parsed_conf.src_port = conf->get_src_port();
    parsed_conf.dst_port = conf->get_dst_port();
    parsed_conf.initial_seq_id = conf->get_initial_seq_id();
    parsed_conf.defer_tx_until_enable_triggers = conf->get_defer_tx_until_enable_triggers();
    parsed_conf.tx_ring_size = conf->get_tx_ring_size();
    parsed_conf.num_bufs = conf->get_num_bufs();
    parsed_conf.mbuf_cache_size = conf->get_mbuf_cache_size();
    parsed_conf.data_room_size = conf->get_data_room_size();

    m_conf = std::move(parsed_conf);
    m_dst_mac = rx_interface->get_mac_address();
    m_dst_ip = rx_interface->get_ip_address().front();
    m_callback_confs = callbacks;

    m_transmitters.clear();
    m_stream_src_ips.clear();
    m_stream_det_ids.clear();
    m_stream_source_ids.clear();
    for (const auto* cb : m_callback_confs) {
      if (cb == nullptr) {
        throw datahandlinglibs::InitializationError(ERS_HERE,
                                                    "DPDKFakeDataSender received a null callback configuration");
      }
      if (cb->get_data_type() != "WIBEthFrame") {
        throw datahandlinglibs::InitializationError(
          ERS_HERE, "DPDKFakeDataSender only supports data_type WIBEthFrame; got " + cb->get_data_type());
      }
      auto transmitter = std::make_unique<transmitter_t>();
      transmitter->add_stream(cb->UID(), m_conf.initial_seq_id);
      m_transmitters.push_back(std::move(transmitter));
      const auto address = detail::stream_address(connection, cb);
      m_stream_src_ips.push_back(address.src_ip);
      m_stream_det_ids.push_back(address.det_id);
      m_stream_source_ids.push_back(static_cast<std::uint32_t>(cb->get_source_id()));
    }
  }

  void validate_configuration() const
  {
    detail::validate_sender_config(
      m_conf, m_dst_mac, m_dst_ip, m_stream_src_ips, m_stream_det_ids, m_stream_source_ids, m_transmitters.size());
  }

  template<typename RegistryT>
  void configure(RegistryT& registry)
  {
    TLOG() << m_name << ": configuring DPDK fake data sender";

    // All pure checks first: a schema-legal but unusable configuration must be
    // rejected before any EAL/NIC side effect happens.
    validate_configuration();

    try {
      setup_eal_and_port();

      for (std::size_t q = 0; q < m_transmitters.size(); ++q) {
        auto& transmitter = *m_transmitters[q];
        auto packet_config = m_packet_config;
        packet_config.src_ip = wibeth::parse_ipv4_addr(m_stream_src_ips[q]);
        transmitter.configure(packet_config,
                              static_cast<std::uint64_t>(m_stream_det_ids[q]),
                              m_runtime.make_tx_backend(m_tx_pools[q], m_tx_port, static_cast<dpdk_id_t>(q)));

        const auto* cb = m_callback_confs[q];
        transmitter.template register_streams<PayloadT>(registry, std::vector<const CallbackConfT*>{ cb });
        TLOG() << m_name << ": WIBEth TX callback armed id=" << cb->UID() << " source_id=" << cb->get_source_id()
               << " tx_queue=" << q << " src_ip=" << m_stream_src_ips[q] << " det_id=" << m_stream_det_ids[q];
      }
    } catch (const std::exception&) {
      // Roll back partially acquired DPDK state so a failed configure leaves
      // the module in a clean, re-configurable state.
      close_dpdk();
      throw;
    }
  }

  void start()
  {
    if (m_conf.defer_tx_until_enable_triggers) {
      TLOG() << m_name << ": DPDK TX remains gated until enable_triggers";
      set_accepting(false);
      return;
    }

    TLOG() << m_name << ": accepting WIBEth frames for DPDK TX";
    set_accepting(true);
  }

  void enable_triggers()
  {
    if (!m_conf.defer_tx_until_enable_triggers) {
      return;
    }

    TLOG() << m_name << ": enable_triggers received; accepting WIBEth frames for DPDK TX";
    set_accepting(true);
  }

  void stop()
  {
    TLOG() << m_name << ": stopping WIBEth TX callback acceptance";
    set_accepting(false);

    std::size_t unknown_stream_drops = 0;
    for (const auto& transmitter : m_transmitters) {
      for (const auto& [id, stats] : transmitter->snapshot_stats()) {
        TLOG() << m_name << ": callback=" << id << " accepted=" << stats.accepted << " sent=" << stats.sent
               << " dropped_not_running=" << stats.dropped_not_running << " tx_alloc_failures=" << stats.tx_alloc_failures
               << " tx_prepare_failures=" << stats.tx_prepare_failures << " tx_failures=" << stats.tx_failures;
      }
      unknown_stream_drops += transmitter->unknown_stream_drops();
    }
    if (unknown_stream_drops > 0) {
      TLOG() << m_name << ": dropped " << unknown_stream_drops
             << " frame(s) for callback ids with no configured stream";
    }
  }

  void scrap()
  {
    TLOG() << m_name << ": scrapping DPDK fake data sender";
    close_dpdk();
  }

  void close_dpdk()
  {
    set_accepting(false);

    // Drain every in-flight callback and block new dispatches before touching
    // any DPDK resource the callbacks might still be using.
    for (auto& transmitter : m_transmitters) {
      transmitter->tombstone();
    }

    if (m_port_configured) {
      m_runtime.eth_dev_stop_and_close(m_tx_port);
      m_port_configured = false;
    }

    // Free the pools (correct deleter) before EAL cleanup; this also releases
    // the pool names so a later configure can recreate them.
    m_tx_pools.clear();

    if (m_eal_initialized) {
      m_runtime.finish_eal();
      m_eal_initialized = false;
    }
  }

  // Read-only state accessors for inspection and unit tests.
  const FakeSenderConfig& config() const { return m_conf; }
  const std::string& dst_mac() const { return m_dst_mac; }
  const std::string& dst_ip() const { return m_dst_ip; }
  const std::vector<std::string>& stream_src_ips() const { return m_stream_src_ips; }
  const std::vector<std::int64_t>& stream_det_ids() const { return m_stream_det_ids; }
  const std::vector<std::unique_ptr<transmitter_t>>& transmitters() const { return m_transmitters; }
  bool eal_initialized() const { return m_eal_initialized; }
  bool port_configured() const { return m_port_configured; }
  dpdk_id_t tx_port() const { return m_tx_port; }
  const wibeth::PacketConfig& packet_config() const { return m_packet_config; }

private:
  void set_accepting(bool accepting)
  {
    for (auto& transmitter : m_transmitters) {
      transmitter->set_accepting(accepting);
    }
  }

  void setup_eal_and_port()
  {
    if (m_port_configured) {
      TLOG() << m_name << ": DPDK TX port is already configured";
      return;
    }

    const auto eal_args =
      detail::build_eal_args(m_conf.tx_pcie, m_conf.eal_lcores, m_conf.eal_args, m_runtime.get_pid());

    m_runtime.init_eal(eal_args);
    m_eal_initialized = true;

    const auto n_ports = m_runtime.eth_dev_count_avail();
    if (n_ports < 1) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "DPDKFakeDataSender EAL found no available ports");
    }

    bool found_port = false;
    for (dpdk_id_t port = 0; port < n_ports; ++port) {
      const auto pcie = m_runtime.get_iface_pci_str(port);
      TLOG() << m_name << ": available TX candidate port=" << port << " pcie=" << pcie;
      if (pcie == m_conf.tx_pcie) {
        m_tx_port = port;
        found_port = true;
        break;
      }
    }
    if (!found_port) {
      throw datahandlinglibs::InitializationError(ERS_HERE,
                                                  "Could not find requested TX PCIe device " + m_conf.tx_pcie);
    }

    const auto n_queues = detail::checked_id(m_transmitters.size(), "TX queue count");
    dpdk_id_t max_tx_queues = 0;
    if (!m_runtime.eth_dev_info_get(m_tx_port, max_tx_queues)) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Could not read device info of TX port");
    }
    TLOG() << m_name << ": TX queues requested=" << n_queues << " max_tx_queues=" << max_tx_queues;
    if (n_queues > max_tx_queues) {
      throw datahandlinglibs::InitializationError(ERS_HERE,
                                                  "stream count " + std::to_string(n_queues) +
                                                    " exceeds max_tx_queues " + std::to_string(max_tx_queues));
    }

    // One pool per TX queue.  Each handle owns its mempool (rte_mempool_free on
    // destruction).  Pool names are limited to RTE_MEMPOOL_NAMESIZE - 1 = 25
    // characters.
    m_tx_pools.clear();
    const int socket_id = m_runtime.eth_dev_socket_id(m_tx_port);
    for (dpdk_id_t q = 0; q < n_queues; ++q) {
      m_tx_pools.push_back(m_runtime.create_mempool("dfs_tx_pool_" + std::to_string(q),
                                                    static_cast<int>(m_conf.num_bufs),
                                                    static_cast<int>(m_conf.mbuf_cache_size),
                                                    static_cast<int>(m_conf.data_room_size),
                                                    socket_id));
    }

    m_runtime.iface_init_tx_only(m_tx_port, n_queues, detail::checked_id(m_conf.tx_ring_size, "tx_ring_size"));
    m_port_configured = true;

    rte_ether_addr src_mac{};
    if (!m_runtime.eth_macaddr_get(m_tx_port, src_mac)) {
      throw datahandlinglibs::InitializationError(ERS_HERE, "Could not read TX port MAC address");
    }

    m_packet_config.src_mac = src_mac;
    m_packet_config.dst_mac = detail::parse_mac(m_dst_mac);
    m_packet_config.dst_ip = wibeth::parse_ipv4_addr(m_dst_ip);
    m_packet_config.src_port = m_conf.src_port;
    m_packet_config.dst_port = m_conf.dst_port;

    TLOG() << m_name << ": TX port configured port=" << m_tx_port << " pcie=" << m_conf.tx_pcie
           << " src_mac=" << detail::format_mac(m_packet_config.src_mac)
           << " dst_mac=" << detail::format_mac(m_packet_config.dst_mac) << " streams=" << m_transmitters.size()
           << " dst_ip=" << m_dst_ip << " src_port=" << m_packet_config.src_port
           << " dst_port=" << m_packet_config.dst_port;
  }

  std::string m_name;
  Runtime m_runtime;

  FakeSenderConfig m_conf;
  std::vector<const CallbackConfT*> m_callback_confs;

  // Index = stream = TX queue, in m_callback_confs order.
  std::vector<std::unique_ptr<transmitter_t>> m_transmitters;
  std::vector<std::string> m_stream_src_ips;
  std::vector<std::int64_t> m_stream_det_ids;
  std::vector<std::uint32_t> m_stream_source_ids;

  // From the net_receiver interface of the connection.
  std::string m_dst_mac;
  std::string m_dst_ip;

  std::vector<pool_handle_t> m_tx_pools;
  dpdk_id_t m_tx_port = 0;
  bool m_eal_initialized = false;
  bool m_port_configured = false;

  wibeth::PacketConfig m_packet_config;
};

} // namespace dunedaq::dpdklibs::sender

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_FAKESENDERCORE_HPP_
