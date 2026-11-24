/**
 * @file DPDKFakeDataSender_test.cxx
 *
 * Offline unit tests for DPDKFakeDataSender helpers, DAL initialization,
 * pre-EAL configuration validation, FSM command gating/teardown, and
 * multi-stream configure wiring/rollback.  All tests run without a real NIC,
 * network send, hugepages, EAL, or OKS database.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "detdataformats/DAQEthHeader.hpp"
#include "dpdklibs/sender/FakeSenderCore.hpp"
#include "dpdklibs/sender/FakeSenderUtils.hpp"
#include "dpdklibs/udp/IPV4UDPPacket.hpp"

#define BOOST_TEST_MODULE DPDKFakeDataSender_test // NOLINT

#include "boost/test/unit_test.hpp"

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace dunedaq::dpdklibs;

namespace {

// ---------------------------------------------------------------------------
// Lightweight DAL fakes matching the exact appmodel/confmodel getter surface
// ---------------------------------------------------------------------------

struct FakeGeoId
{
  std::uint32_t detector_id = 3;
  std::uint32_t get_detector_id() const { return detector_id; }
};

struct FakeDetectorStream
{
  std::uint32_t source_id = 100;
  FakeGeoId geo_id;

  std::uint32_t get_source_id() const { return source_id; }
  const FakeGeoId* get_geo_id() const { return &geo_id; }
};

struct FakeNetworkInterface
{
  std::string uid = "net-iface";
  std::string mac_address = "6c:fe:54:47:a2:50";
  std::vector<std::string> ip_addresses = { "192.168.100.2" };

  const std::string& UID() const { return uid; }
  const std::string& get_mac_address() const { return mac_address; }
  const std::vector<std::string>& get_ip_address() const { return ip_addresses; }
};

struct FakeNetSender
{
  std::vector<const FakeDetectorStream*> streams;
  const FakeNetworkInterface* uses = nullptr;

  const std::vector<const FakeDetectorStream*>& get_streams() const { return streams; }
  const FakeNetworkInterface* get_uses() const { return uses; }
};

struct FakeNetReceiver
{
  const FakeNetworkInterface* uses = nullptr;
  const FakeNetworkInterface* get_uses() const { return uses; }
};

struct FakeConnection
{
  std::string uid = "test-conn";
  const FakeNetReceiver* net_receiver = nullptr;
  std::vector<const FakeNetSender*> net_senders;

  const std::string& UID() const { return uid; }
  const FakeNetReceiver* get_net_receiver() const { return net_receiver; }
  const std::vector<const FakeNetSender*>& get_net_senders() const { return net_senders; }
};

struct FakeCallbackConf
{
  std::string uid;
  std::uint32_t source_id = 100;
  std::string data_type = "WIBEthFrame";

  const std::string& UID() const { return uid; }
  std::uint32_t get_source_id() const { return source_id; }
  const std::string& get_data_type() const { return data_type; }
};

struct FakeSenderConfDal
{
  std::string eal_args = "--no-telemetry";
  std::string eal_lcores = "0-2";
  std::string tx_pcie = "0000:41:00.0";
  std::uint16_t src_port = 55677;
  std::uint16_t dst_port = 55678;
  std::uint32_t initial_seq_id = 0;
  bool defer_tx_until_enable_triggers = false;
  std::uint32_t tx_ring_size = 1024;
  std::uint32_t num_bufs = 2047;
  std::uint32_t mbuf_cache_size = 250;
  std::uint32_t data_room_size = 9800;

  const std::string& get_eal_args() const { return eal_args; }
  const std::string& get_eal_lcores() const { return eal_lcores; }
  const std::string& get_tx_pcie() const { return tx_pcie; }
  std::uint16_t get_src_port() const { return src_port; }
  std::uint16_t get_dst_port() const { return dst_port; }
  std::uint32_t get_initial_seq_id() const { return initial_seq_id; }
  bool get_defer_tx_until_enable_triggers() const { return defer_tx_until_enable_triggers; }
  std::uint32_t get_tx_ring_size() const { return tx_ring_size; }
  std::uint32_t get_num_bufs() const { return num_bufs; }
  std::uint32_t get_mbuf_cache_size() const { return mbuf_cache_size; }
  std::uint32_t get_data_room_size() const { return data_room_size; }
};

struct FakeModuleDal
{
  const FakeSenderConfDal* configuration = nullptr;
  const FakeConnection* connection = nullptr;
  std::vector<const FakeCallbackConf*> callbacks;

  const FakeSenderConfDal* get_configuration() const { return configuration; }
  const FakeConnection* get_connection() const { return connection; }
  const std::vector<const FakeCallbackConf*>& get_callbacks() const { return callbacks; }
};

struct FakeConfigManager
{
  std::map<std::string, const FakeModuleDal*> modules;

  template<typename T>
  const T* get_dal(const std::string& name) const
  {
    const auto it = modules.find(name);
    if (it == modules.end()) {
      return nullptr;
    }
    return it->second;
  }
};

// ---------------------------------------------------------------------------
// Fake payload, TX backend, callback registry, and DPDK runtime seam
// ---------------------------------------------------------------------------

struct FakePayload
{
  char data[wibeth::kWIBEthFrameBytes] = {};
};

struct FakeTxBackend
{
  std::string pool_name;
  sender::detail::dpdk_id_t port = 0;
  sender::detail::dpdk_id_t queue = 0;
  int alloc_calls = 0;
  int tx_calls = 0;
  int free_calls = 0;
  std::array<std::uint8_t, wibeth::kEthernetPacketBytes> last_packet{};

  void* alloc()
  {
    ++alloc_calls;
    return last_packet.data();
  }
  std::uint8_t* prepare(void* buf, std::size_t /*bytes*/) { return static_cast<std::uint8_t*>(buf); }
  bool transmit(void* /*buf*/)
  {
    ++tx_calls;
    return true;
  }
  void free(void* /*buf*/) { ++free_calls; }
};

struct FakeCallbackRegistry
{
  std::map<std::string, std::function<void(FakePayload&&)>> callbacks;
  int register_attempts = 0;
  int rejected_duplicates = 0;
  std::string throw_on_uid;

  template<typename DataType>
  void register_callback(const FakeCallbackConf* conf, std::function<void(DataType&&)> callback)
  {
    ++register_attempts;
    if (!throw_on_uid.empty() && conf->UID() == throw_on_uid) {
      throw std::runtime_error("Injected registry failure for " + conf->UID());
    }
    if (callbacks.count(conf->UID()) != 0) {
      ++rejected_duplicates;
      return;
    }
    callbacks[conf->UID()] = std::move(callback);
  }
};

rte_ether_addr
make_mac(const std::array<std::uint8_t, 6>& bytes)
{
  rte_ether_addr addr{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    addr.addr_bytes[i] = bytes[i];
  }
  return addr;
}

struct FakePortInfo
{
  std::string pcie;
  sender::detail::dpdk_id_t max_tx_queues = 64;
  int socket_id = 0;
  rte_ether_addr mac = make_mac({ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 });
};

struct MempoolCreationRecord
{
  std::string name;
  int num_bufs = 0;
  int mbuf_cache_size = 0;
  int data_room_size = 0;
  int socket_id = 0;
};

struct IfaceInitRecord
{
  sender::detail::dpdk_id_t port = 0;
  sender::detail::dpdk_id_t n_tx_queues = 0;
  sender::detail::dpdk_id_t tx_ring_size = 0;
};

struct FakeDpdkState
{
  pid_t pid = 4242;
  std::vector<FakePortInfo> ports = {
    FakePortInfo{ "0000:41:00.0", 64, 1, make_mac({ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 }) }
  };

  // Scriptable failure hooks.
  bool fail_dev_info = false;
  bool fail_macaddr = false;
  bool fail_iface_init = false;
  int fail_mempool_on_call = -1; // 0-based index within create_mempool calls (resettable per test)

  // Call counters & recordings.
  int init_eal_calls = 0;
  int finish_eal_calls = 0;
  int create_mempool_calls = 0;
  int iface_init_calls = 0;
  int stop_close_calls = 0;
  int live_pools = 0;

  std::vector<std::string> last_eal_args;
  std::vector<MempoolCreationRecord> created_mempools;
  std::vector<IfaceInitRecord> iface_inits;
  std::vector<sender::detail::dpdk_id_t> stopped_ports;
  std::vector<std::string> events;
};

struct FakePoolHandle
{
  FakeDpdkState* state = nullptr;
  std::string name;

  FakePoolHandle() = default;
  FakePoolHandle(FakeDpdkState* s, std::string n)
    : state(s)
    , name(std::move(n))
  {
    if (state != nullptr) {
      ++state->live_pools;
    }
  }
  FakePoolHandle(const FakePoolHandle&) = delete;
  FakePoolHandle& operator=(const FakePoolHandle&) = delete;
  FakePoolHandle(FakePoolHandle&& other) noexcept
    : state(other.state)
    , name(std::move(other.name))
  {
    other.state = nullptr;
    other.name.clear();
  }
  FakePoolHandle& operator=(FakePoolHandle&& other) noexcept
  {
    if (this != &other) {
      release_pool();
      state = other.state;
      name = std::move(other.name);
      other.state = nullptr;
      other.name.clear();
    }
    return *this;
  }
  ~FakePoolHandle() { release_pool(); }

private:
  void release_pool()
  {
    if (state != nullptr) {
      --state->live_pools;
      state->events.push_back("free_pool:" + name);
      state = nullptr;
    }
  }
};

struct FakeDpdkRuntime
{
  using pool_handle_t = FakePoolHandle;
  using dpdk_id_t = sender::detail::dpdk_id_t;

  FakeDpdkState* state = nullptr;

  pid_t get_pid() const { return state->pid; }

  void init_eal(const std::vector<std::string>& args)
  {
    ++state->init_eal_calls;
    state->last_eal_args = args;
    state->events.push_back("init_eal");
  }

  void finish_eal()
  {
    ++state->finish_eal_calls;
    state->events.push_back("finish_eal");
  }

  dpdk_id_t eth_dev_count_avail() const { return static_cast<dpdk_id_t>(state->ports.size()); }

  std::string get_iface_pci_str(dpdk_id_t port) const { return state->ports.at(port).pcie; }

  bool eth_dev_info_get(dpdk_id_t port, dpdk_id_t& max_tx_queues) const
  {
    if (state->fail_dev_info) {
      return false;
    }
    max_tx_queues = state->ports.at(port).max_tx_queues;
    return true;
  }

  int eth_dev_socket_id(dpdk_id_t port) const { return state->ports.at(port).socket_id; }

  pool_handle_t create_mempool(const std::string& name,
                               int num_bufs,
                               int mbuf_cache_size,
                               int data_room_size,
                               int socket_id)
  {
    const int call_idx = state->create_mempool_calls++;
    if (state->fail_mempool_on_call == call_idx) {
      throw std::runtime_error("Injected mempool creation failure for " + name);
    }
    state->created_mempools.push_back({ name, num_bufs, mbuf_cache_size, data_room_size, socket_id });
    state->events.push_back("create_mempool:" + name);
    return FakePoolHandle(state, name);
  }

  void iface_init_tx_only(dpdk_id_t port, dpdk_id_t n_tx_queues, dpdk_id_t tx_ring_size)
  {
    ++state->iface_init_calls;
    if (state->fail_iface_init) {
      throw std::runtime_error("Injected iface_init failure");
    }
    state->iface_inits.push_back({ port, n_tx_queues, tx_ring_size });
    state->events.push_back("iface_init");
  }

  bool eth_macaddr_get(dpdk_id_t port, rte_ether_addr& addr) const
  {
    if (state->fail_macaddr) {
      return false;
    }
    addr = state->ports.at(port).mac;
    return true;
  }

  void eth_dev_stop_and_close(dpdk_id_t port)
  {
    ++state->stop_close_calls;
    state->stopped_ports.push_back(port);
    state->events.push_back("eth_dev_stop_and_close");
  }

  FakeTxBackend make_tx_backend(const pool_handle_t& pool, dpdk_id_t port, dpdk_id_t queue) const
  {
    FakeTxBackend backend;
    backend.pool_name = pool.name;
    backend.port = port;
    backend.queue = queue;
    return backend;
  }
};

using test_core_t = sender::FakeSenderCore<FakeDpdkRuntime, FakeTxBackend, FakePayload, FakeCallbackConf>;

// Reusable fixture holding a valid 3-stream, 2-sender DAL graph.
struct SampleDalFixture
{
  FakeNetworkInterface rx_iface{ "rx-iface", "6c:fe:54:47:a2:50", { "192.168.100.2", "192.168.100.99" } };
  FakeNetReceiver receiver{ &rx_iface };

  FakeNetworkInterface tx_iface_0{ "tx-iface-0", "00:00:00:00:00:00", { "192.168.100.11", "192.168.100.111" } };
  FakeNetworkInterface tx_iface_1{ "tx-iface-1", "00:00:00:00:00:00", { "192.168.100.12" } };

  FakeDetectorStream stream_0{ 100, FakeGeoId{ 3 } };
  FakeDetectorStream stream_1{ 101, FakeGeoId{ 4 } };
  FakeDetectorStream stream_2{ 102, FakeGeoId{ 7 } };

  FakeNetSender sender_0;
  FakeNetSender sender_1;
  FakeConnection conn;

  FakeCallbackConf cb_0{ "cb-100", 100, "WIBEthFrame" };
  FakeCallbackConf cb_1{ "cb-101", 101, "WIBEthFrame" };
  FakeCallbackConf cb_2{ "cb-102", 102, "WIBEthFrame" };

  FakeSenderConfDal conf;
  FakeModuleDal module_dal;
  FakeConfigManager cfg;

  SampleDalFixture()
  {
    sender_0.streams = { &stream_0, &stream_1 };
    sender_0.uses = &tx_iface_0;
    sender_1.streams = { &stream_2 };
    sender_1.uses = &tx_iface_1;

    conn.uid = "conn-0";
    conn.net_receiver = &receiver;
    conn.net_senders = { &sender_0, &sender_1 };

    module_dal.configuration = &conf;
    module_dal.connection = &conn;
    module_dal.callbacks = { &cb_0, &cb_1, &cb_2 };

    cfg.modules["sender-0"] = &module_dal;
  }
};

std::size_t
event_index(const std::vector<std::string>& events, const std::string& target)
{
  const auto it = std::find(events.begin(), events.end(), target);
  BOOST_REQUIRE_MESSAGE(it != events.end(), "Expected event not found: " + target);
  return static_cast<std::size_t>(it - events.begin());
}

} // namespace

BOOST_AUTO_TEST_SUITE(DPDKFakeDataSender_test)

// ===========================================================================
// Category 1: Pure helpers in dunedaq::dpdklibs::sender::detail
// ===========================================================================

BOOST_AUTO_TEST_CASE(SplitArgsHandlesEmptyWhitespaceAndMixedDelimiters)
{
  BOOST_CHECK(sender::detail::split_args("").empty());
  BOOST_CHECK(sender::detail::split_args("   \t\n\r  ").empty());

  const auto single = sender::detail::split_args("--no-telemetry");
  BOOST_REQUIRE_EQUAL(single.size(), 1);
  BOOST_CHECK_EQUAL(single[0], "--no-telemetry");

  const auto multi = sender::detail::split_args("  --no-telemetry\t--in-memory\n  --log-level=7   -n 4  ");
  const std::vector<std::string> expected{ "--no-telemetry", "--in-memory", "--log-level=7", "-n", "4" };
  BOOST_CHECK_EQUAL_COLLECTIONS(multi.begin(), multi.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(BuildEalArgsAssemblesRequiredAndExtraFlags)
{
  const auto args = sender::detail::build_eal_args("0000:41:00.0", "0-2", "--no-telemetry --in-memory", 12345);
  const std::vector<std::string> expected{ "dpdklibs_dpdk_fake_data_sender",
                                           "--proc-type=primary",
                                           "-a",
                                           "0000:41:00.0",
                                           "-l",
                                           "0-2",
                                           "--file-prefix",
                                           "dpdklibs_fake_sender_12345",
                                           "--no-telemetry",
                                           "--in-memory" };
  BOOST_CHECK_EQUAL_COLLECTIONS(args.begin(), args.end(), expected.begin(), expected.end());

  const auto no_extra = sender::detail::build_eal_args("0000:af:00.0", "11", "   ", 99);
  BOOST_REQUIRE_EQUAL(no_extra.size(), 8);
  BOOST_CHECK_EQUAL(no_extra[3], "0000:af:00.0");
  BOOST_CHECK_EQUAL(no_extra[5], "11");
  BOOST_CHECK_EQUAL(no_extra[7], "dpdklibs_fake_sender_99");
}

BOOST_AUTO_TEST_CASE(CheckedIdBoundariesAndOverflow)
{
  BOOST_CHECK_EQUAL(sender::detail::checked_id(0U, "zero"), 0U);
  BOOST_CHECK_EQUAL(sender::detail::checked_id(65535U, "max_u16"), 65535U);
  BOOST_CHECK_EQUAL(sender::detail::checked_id(std::size_t{ 65535 }, "max_size"), 65535U);

  BOOST_CHECK_THROW(sender::detail::checked_id(65536U, "overflow_1"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::checked_id(std::numeric_limits<std::uint32_t>::max(), "overflow_u32"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::checked_id(-1, "negative"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(ParseMacValidAndMalformedInputs)
{
  const auto lower = sender::detail::parse_mac("6c:fe:54:47:a2:50");
  const std::array<std::uint8_t, 6> expected{ 0x6c, 0xfe, 0x54, 0x47, 0xa2, 0x50 };
  BOOST_CHECK_EQUAL_COLLECTIONS(
    lower.addr_bytes, lower.addr_bytes + 6, expected.begin(), expected.end());

  const auto upper = sender::detail::parse_mac("6C:FE:54:47:A2:50");
  BOOST_CHECK_EQUAL_COLLECTIONS(
    upper.addr_bytes, upper.addr_bytes + 6, expected.begin(), expected.end());

  const auto mixed = sender::detail::parse_mac("0a:Bc:00:Ff:01:9e");
  const std::array<std::uint8_t, 6> expected_mixed{ 0x0a, 0xbc, 0x00, 0xff, 0x01, 0x9e };
  BOOST_CHECK_EQUAL_COLLECTIONS(
    mixed.addr_bytes, mixed.addr_bytes + 6, expected_mixed.begin(), expected_mixed.end());

  const auto zero = sender::detail::parse_mac("00:00:00:00:00:00");
  const std::array<std::uint8_t, 6> expected_zero{ 0, 0, 0, 0, 0, 0 };
  BOOST_CHECK_EQUAL_COLLECTIONS(
    zero.addr_bytes, zero.addr_bytes + 6, expected_zero.begin(), expected_zero.end());

  const auto broadcast = sender::detail::parse_mac("ff:ff:ff:ff:ff:ff");
  const std::array<std::uint8_t, 6> expected_bcast{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  BOOST_CHECK_EQUAL_COLLECTIONS(
    broadcast.addr_bytes, broadcast.addr_bytes + 6, expected_bcast.begin(), expected_bcast.end());

  // Malformed MAC strings.
  BOOST_CHECK_THROW(sender::detail::parse_mac(""), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("aa:bb:cc:dd:ee"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("aa:bb:cc:dd:ee:ff:00"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("a:bb:cc:dd:ee:ff"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("aa-bb-cc-dd-ee-ff"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("gg:bb:cc:dd:ee:ff"), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("aa:bb:cc:dd:ee:ff "), std::runtime_error);
  BOOST_CHECK_THROW(sender::detail::parse_mac("aa:bb:cc:dd:ee:ff:"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(FormatMacPadsAndRoundTripsWithParseMac)
{
  const rte_ether_addr addr = make_mac({ 0x00, 0x05, 0x0a, 0x0f, 0x10, 0xff });
  const std::string formatted = sender::detail::format_mac(addr);
  BOOST_CHECK_EQUAL(formatted, "00:05:0a:0f:10:ff");

  const rte_ether_addr parsed = sender::detail::parse_mac(formatted);
  BOOST_CHECK_EQUAL_COLLECTIONS(
    parsed.addr_bytes, parsed.addr_bytes + 6, addr.addr_bytes, addr.addr_bytes + 6);
}

BOOST_AUTO_TEST_CASE(StreamAddressResolvesAcrossSendersAndErrorsOnMissingOrEmptyIp)
{
  SampleDalFixture fx;

  // Stream on first sender with multiple IPs: must pick addresses.front().
  const auto addr0 = sender::detail::stream_address(&fx.conn, &fx.cb_0);
  BOOST_CHECK_EQUAL(addr0.src_ip, "192.168.100.11");
  BOOST_CHECK_EQUAL(addr0.det_id, 3);

  // Second stream on first sender.
  const auto addr1 = sender::detail::stream_address(&fx.conn, &fx.cb_1);
  BOOST_CHECK_EQUAL(addr1.src_ip, "192.168.100.11");
  BOOST_CHECK_EQUAL(addr1.det_id, 4);

  // Stream on second sender.
  const auto addr2 = sender::detail::stream_address(&fx.conn, &fx.cb_2);
  BOOST_CHECK_EQUAL(addr2.src_ip, "192.168.100.12");
  BOOST_CHECK_EQUAL(addr2.det_id, 7);

  // Missing source_id.
  FakeCallbackConf unknown_cb{ "cb-999", 999, "WIBEthFrame" };
  BOOST_CHECK_THROW(sender::detail::stream_address(&fx.conn, &unknown_cb), dunedaq::datahandlinglibs::InitializationError);

  // Matched sender with no ip_address.
  fx.tx_iface_1.ip_addresses.clear();
  BOOST_CHECK_THROW(sender::detail::stream_address(&fx.conn, &fx.cb_2), dunedaq::datahandlinglibs::InitializationError);
}

// ===========================================================================
// Category 2: DPDKFakeDataSender::init() (OKS/DAL loading; zero EAL)
// ===========================================================================

BOOST_AUTO_TEST_CASE(InitHappyPathLoadsReceiverAndStreamsInCallbackOrder)
{
  SampleDalFixture fx;
  fx.conf.initial_seq_id = 42;
  // Reverse callback order relative to connection streams to verify callback order wins.
  fx.module_dal.callbacks = { &fx.cb_2, &fx.cb_0, &fx.cb_1 };

  FakeDpdkState state;
  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);

  BOOST_CHECK_EQUAL(core.dst_mac(), "6c:fe:54:47:a2:50");
  BOOST_CHECK_EQUAL(core.dst_ip(), "192.168.100.2"); // first IP of net_receiver
  BOOST_REQUIRE_EQUAL(core.transmitters().size(), 3);
  BOOST_REQUIRE_EQUAL(core.stream_src_ips().size(), 3);
  BOOST_REQUIRE_EQUAL(core.stream_det_ids().size(), 3);

  BOOST_CHECK_EQUAL(core.stream_src_ips()[0], "192.168.100.12");
  BOOST_CHECK_EQUAL(core.stream_det_ids()[0], 7);
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-102").next_seq, 42);

  BOOST_CHECK_EQUAL(core.stream_src_ips()[1], "192.168.100.11");
  BOOST_CHECK_EQUAL(core.stream_det_ids()[1], 3);
  BOOST_CHECK_EQUAL(core.transmitters()[1]->snapshot_stats().at("cb-100").next_seq, 42);

  BOOST_CHECK_EQUAL(core.stream_src_ips()[2], "192.168.100.11");
  BOOST_CHECK_EQUAL(core.stream_det_ids()[2], 4);
  BOOST_CHECK_EQUAL(core.transmitters()[2]->snapshot_stats().at("cb-101").next_seq, 42);

  BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
}

BOOST_AUTO_TEST_CASE(InitReInitClearsPriorStreams)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  test_core_t core("sender-0", FakeDpdkRuntime{ &state });

  core.init<FakeModuleDal>(fx.cfg);
  BOOST_REQUIRE_EQUAL(core.transmitters().size(), 3);

  // Re-init with a single callback: vectors must be replaced, not appended.
  fx.module_dal.callbacks = { &fx.cb_2 };
  core.init<FakeModuleDal>(fx.cfg);

  BOOST_REQUIRE_EQUAL(core.transmitters().size(), 1);
  BOOST_REQUIRE_EQUAL(core.stream_src_ips().size(), 1);
  BOOST_REQUIRE_EQUAL(core.stream_det_ids().size(), 1);
  BOOST_CHECK_EQUAL(core.stream_src_ips()[0], "192.168.100.12");
  BOOST_CHECK_EQUAL(core.stream_det_ids()[0], 7);
}

BOOST_AUTO_TEST_CASE(InitRejectsMissingOrInvalidDalObjects)
{
  FakeDpdkState state;

  // Missing module DAL object for module name.
  {
    SampleDalFixture fx;
    test_core_t core("nonexistent-sender", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }

  // Missing DPDKFakeDataSenderConf (nullptr).
  {
    SampleDalFixture fx;
    fx.module_dal.configuration = nullptr;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }

  // net_receiver NetworkInterface has no ip_address.
  {
    SampleDalFixture fx;
    fx.rx_iface.ip_addresses.clear();
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }

  // Empty callbacks list.
  {
    SampleDalFixture fx;
    fx.module_dal.callbacks.clear();
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }

  // Null callback entry.
  {
    SampleDalFixture fx;
    fx.module_dal.callbacks = { &fx.cb_0, nullptr };
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }

  // Callback source_id not present in connection's net_senders.
  {
    SampleDalFixture fx;
    FakeCallbackConf orphan_cb{ "cb-orphan", 999, "WIBEthFrame" };
    fx.module_dal.callbacks = { &fx.cb_0, &orphan_cb };
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }

  // Matched sender's NetworkInterface has empty ip_address.
  {
    SampleDalFixture fx;
    fx.tx_iface_1.ip_addresses.clear();
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    BOOST_CHECK_THROW(core.init<FakeModuleDal>(fx.cfg), dunedaq::datahandlinglibs::InitializationError);
  }
}

// ===========================================================================
// Category 3: Pre-EAL configuration validation (zero EAL)
// ===========================================================================

BOOST_AUTO_TEST_CASE(ValidateDataRoomSizeBoundsPreEal)
{
  constexpr std::uint32_t kMinRoom =
    static_cast<std::uint32_t>(RTE_PKTMBUF_HEADROOM + wibeth::kEthernetPacketBytes);
  BOOST_REQUIRE_EQUAL(kMinRoom, 7370U);

  // Rejects 7369 (below minimum).
  {
    SampleDalFixture fx;
    fx.conf.data_room_size = kMinRoom - 1;
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }

  // Rejects 65536 (exceeds uint16_t max).
  {
    SampleDalFixture fx;
    fx.conf.data_room_size = 65536;
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }

  // Accepts exact lower boundary 7370 and exact upper boundary 65535.
  for (const std::uint32_t boundary : { kMinRoom, std::uint32_t{ 65535 } }) {
    SampleDalFixture fx;
    fx.conf.data_room_size = boundary;
    FakeDpdkState state;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_NO_THROW(core.validate_configuration());
  }
}

BOOST_AUTO_TEST_CASE(ValidateDetectorIdSixBitRangePreEal)
{
  // Accepts 0 and 63.
  {
    SampleDalFixture fx;
    fx.stream_0.geo_id.detector_id = 0;
    fx.stream_1.geo_id.detector_id = 63;
    fx.stream_2.geo_id.detector_id = 31;
    FakeDpdkState state;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_NO_THROW(core.validate_configuration());
  }

  // Rejects 64 on a non-first stream (q > 0) before EAL.
  {
    SampleDalFixture fx;
    fx.stream_0.geo_id.detector_id = 3;
    fx.stream_1.geo_id.detector_id = 63;
    fx.stream_2.geo_id.detector_id = 64;
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }
}

BOOST_AUTO_TEST_CASE(ValidateDestinationMacRejectsMalformedAndAcceptsValidPreEal)
{
  // Rejects malformed MAC strings before EAL.
  for (const std::string& bad_mac : { std::string(""), std::string("not-a-mac"), std::string("6c:fe:54:47:a2") }) {
    SampleDalFixture fx;
    fx.rx_iface.mac_address = bad_mac;
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }

  // Accepts valid non-zero MAC.
  SampleDalFixture fx;
  fx.rx_iface.mac_address = "6c:fe:54:47:a2:50";
  FakeDpdkState state;
  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  BOOST_CHECK_NO_THROW(core.validate_configuration());
}

BOOST_AUTO_TEST_CASE(ValidateSixteenBitIdLimitsPreEal)
{
  // tx_ring_size > 65535.
  {
    SampleDalFixture fx;
    fx.conf.tx_ring_size = 65536;
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }

  // TX queue count > 65535 (tested directly via validate_sender_config without allocating 65k transmitters).
  {
    sender::FakeSenderConfig conf;
    BOOST_CHECK_NO_THROW(sender::detail::validate_sender_config(
      conf, "6c:fe:54:47:a2:50", "192.168.100.2", { "192.168.100.11" }, { 3 }, { 100 }, 65535));
    BOOST_CHECK_THROW(sender::detail::validate_sender_config(
                        conf, "6c:fe:54:47:a2:50", "192.168.100.2", { "192.168.100.11" }, { 3 }, { 100 }, 65536),
                      std::runtime_error);
  }
}

BOOST_AUTO_TEST_CASE(ValidateIpv4AddressesPreEal)
{
  // Malformed dst_ip rejected before EAL.
  {
    SampleDalFixture fx;
    fx.rx_iface.ip_addresses = { "not-an-ip" };
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }

  // Malformed per-stream src_ip (on non-first stream q > 0) rejected before EAL.
  {
    SampleDalFixture fx;
    fx.tx_iface_1.ip_addresses = { "999.168.100.12" };
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
    BOOST_CHECK(registry.callbacks.empty());
  }
}

BOOST_AUTO_TEST_CASE(ValidationFailureLeavesNoPartialStateAndScrapIsNoOp)
{
  SampleDalFixture fx;
  fx.conf.data_room_size = 1000;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  {
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);

    BOOST_CHECK(!core.eal_initialized());
    BOOST_CHECK(!core.port_configured());
    BOOST_CHECK_NO_THROW(core.scrap());
  }

  // Neither scrap() nor destructor invoked any DPDK teardown call.
  BOOST_CHECK_EQUAL(state.init_eal_calls, 0);
  BOOST_CHECK_EQUAL(state.finish_eal_calls, 0);
  BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
  BOOST_CHECK_EQUAL(state.live_pools, 0);
  BOOST_CHECK(state.events.empty());
  BOOST_CHECK(registry.callbacks.empty());
}

// ===========================================================================
// Category 4: FSM command gating & lifecycle teardown (zero real EAL)
// ===========================================================================

BOOST_AUTO_TEST_CASE(FsmDeferredTxGatesUntilEnableTriggersAndReGatesOnNextStart)
{
  SampleDalFixture fx;
  fx.conf.defer_tx_until_enable_triggers = true;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  core.configure(registry);

  // start() leaves all streams gated when defer_tx_until_enable_triggers == true.
  core.start();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(!tx->accepting());
  }
  registry.callbacks.at("cb-100")(FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").dropped_not_running, 1);
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").sent, 0);

  // enable_triggers() opens acceptance on all streams.
  core.enable_triggers();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(tx->accepting());
  }
  registry.callbacks.at("cb-100")(FakePayload{});
  registry.callbacks.at("cb-102")(FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").sent, 1);
  BOOST_CHECK_EQUAL(core.transmitters()[2]->snapshot_stats().at("cb-102").sent, 1);

  // stop() closes acceptance on all streams.
  core.stop();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(!tx->accepting());
  }

  // Subsequent start() re-gates all streams until the next enable_triggers().
  core.enable_triggers();
  BOOST_REQUIRE(core.transmitters()[0]->accepting());
  core.start();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(!tx->accepting());
  }
}

BOOST_AUTO_TEST_CASE(FsmImmediateTxAcceptsOnStartAndEnableTriggersIsNoOp)
{
  SampleDalFixture fx;
  fx.conf.defer_tx_until_enable_triggers = false;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  core.configure(registry);

  core.start();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(tx->accepting());
  }
  registry.callbacks.at("cb-101")(FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[1]->snapshot_stats().at("cb-101").sent, 1);

  core.stop();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(!tx->accepting());
  }

  // With defer_tx_until_enable_triggers == false, enable_triggers() must be a no-op
  // and must NOT re-enable accepting after stop().
  core.enable_triggers();
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(!tx->accepting());
  }
}

BOOST_AUTO_TEST_CASE(FsmStopSnapshotsStatsAndUnknownStreamDropsSafely)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  core.configure(registry);
  core.start();

  // Trigger an unknown-stream drop directly on transmitter 0 so stop() exercises
  // the unknown_stream_drops > 0 reporting path too.
  core.transmitters()[0]->transmit("unknown-callback-id", FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[0]->unknown_stream_drops(), 1);

  BOOST_CHECK_NO_THROW(core.stop());
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(!tx->accepting());
  }
}

BOOST_AUTO_TEST_CASE(TeardownScrapAndDestructorAreIdempotentAndTombstoneTransmitters)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  {
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    // scrap() before init() and before configure() is a safe no-op.
    BOOST_CHECK_NO_THROW(core.scrap());
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_NO_THROW(core.scrap());
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 0);
    BOOST_CHECK_EQUAL(state.stop_close_calls, 0);

    core.configure(registry);
    core.start();
    registry.callbacks.at("cb-100")(FakePayload{});
    BOOST_REQUIRE_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").sent, 1);

    // First scrap tears down DPDK, clears accepting, and tombstones all transmitters.
    core.scrap();
    BOOST_CHECK(!core.port_configured());
    BOOST_CHECK(!core.eal_initialized());
    BOOST_CHECK_EQUAL(state.stop_close_calls, 1);
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.live_pools, 0);
    for (const auto& tx : core.transmitters()) {
      BOOST_CHECK(!tx->accepting());
      BOOST_CHECK(!tx->armed());
    }

    // Dispatches through the pinned registry after scrap become safe drops.
    registry.callbacks.at("cb-100")(FakePayload{});
    BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").accepted, 1);

    // Repeated scrap() calls are idempotent no-ops.
    core.scrap();
    core.scrap();
    BOOST_CHECK_EQUAL(state.stop_close_calls, 1);
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
  }

  // Destructor after scrap() is also an idempotent no-op.
  BOOST_CHECK_EQUAL(state.stop_close_calls, 1);
  BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
  BOOST_CHECK_EQUAL(state.live_pools, 0);
}

// ===========================================================================
// Category 5: configure() multi-stream wiring, port discovery & rollback
// ===========================================================================

BOOST_AUTO_TEST_CASE(ConfigureMultiStreamFanOutWiresDistinctSrcIpDetIdQueueAndMempool)
{
  SampleDalFixture fx;
  fx.conf.tx_pcie = "0000:41:00.0";
  fx.conf.src_port = 40001;
  fx.conf.dst_port = 40002;
  fx.conf.initial_seq_id = 17;
  fx.conf.tx_ring_size = 512;
  fx.conf.num_bufs = 1024;
  fx.conf.mbuf_cache_size = 128;
  fx.conf.data_room_size = 9000;

  FakeDpdkState state;
  state.ports = {
    FakePortInfo{ "0000:11:00.0", 32, 0, make_mac({ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 }) },
    FakePortInfo{ "0000:41:00.0", 64, 2, make_mac({ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x02 }) },
  };
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  core.configure(registry);
  core.start();

  BOOST_CHECK_EQUAL(core.tx_port(), 1U);
  BOOST_REQUIRE_EQUAL(state.created_mempools.size(), 3);
  for (std::size_t q = 0; q < 3; ++q) {
    BOOST_CHECK_EQUAL(state.created_mempools[q].name, "dfs_tx_pool_" + std::to_string(q));
    BOOST_CHECK_EQUAL(state.created_mempools[q].num_bufs, 1024);
    BOOST_CHECK_EQUAL(state.created_mempools[q].mbuf_cache_size, 128);
    BOOST_CHECK_EQUAL(state.created_mempools[q].data_room_size, 9000);
    BOOST_CHECK_EQUAL(state.created_mempools[q].socket_id, 2);
  }
  BOOST_REQUIRE_EQUAL(state.iface_inits.size(), 1);
  BOOST_CHECK_EQUAL(state.iface_inits[0].port, 1U);
  BOOST_CHECK_EQUAL(state.iface_inits[0].n_tx_queues, 3U);
  BOOST_CHECK_EQUAL(state.iface_inits[0].tx_ring_size, 512U);

  // Dispatch one frame per stream and inspect each stream's backend & wire packet.
  const std::array<std::string, 3> uids{ "cb-100", "cb-101", "cb-102" };
  const std::array<std::string, 3> expected_src_ips{ "192.168.100.11", "192.168.100.11", "192.168.100.12" };
  const std::array<std::uint64_t, 3> expected_det_ids{ 3, 4, 7 };
  const rte_ether_addr expected_dst_mac = sender::detail::parse_mac("6c:fe:54:47:a2:50");
  const rte_ether_addr expected_src_mac = state.ports[1].mac;
  const rte_be32_t expected_dst_ip = wibeth::parse_ipv4_addr("192.168.100.2");

  for (std::size_t q = 0; q < 3; ++q) {
    registry.callbacks.at(uids[q])(FakePayload{});

    const auto& backend = core.transmitters()[q]->backend();
    BOOST_CHECK_EQUAL(backend.pool_name, "dfs_tx_pool_" + std::to_string(q));
    BOOST_CHECK_EQUAL(backend.port, 1U);
    BOOST_CHECK_EQUAL(backend.queue, q);
    BOOST_CHECK_EQUAL(backend.tx_calls, 1);

    udp::ipv4_udp_packet_hdr hdr{};
    std::memcpy(&hdr, backend.last_packet.data(), sizeof(hdr));
    BOOST_CHECK_EQUAL_COLLECTIONS(
      hdr.eth_hdr.dst_addr.addr_bytes, hdr.eth_hdr.dst_addr.addr_bytes + 6, expected_dst_mac.addr_bytes, expected_dst_mac.addr_bytes + 6);
    BOOST_CHECK_EQUAL_COLLECTIONS(
      hdr.eth_hdr.src_addr.addr_bytes, hdr.eth_hdr.src_addr.addr_bytes + 6, expected_src_mac.addr_bytes, expected_src_mac.addr_bytes + 6);
    BOOST_CHECK_EQUAL(hdr.ipv4_hdr.src_addr, wibeth::parse_ipv4_addr(expected_src_ips[q]));
    BOOST_CHECK_EQUAL(hdr.ipv4_hdr.dst_addr, expected_dst_ip);
    BOOST_CHECK_EQUAL(rte_be_to_cpu_16(hdr.udp_hdr.src_port), 40001U);
    BOOST_CHECK_EQUAL(rte_be_to_cpu_16(hdr.udp_hdr.dst_port), 40002U);

    dunedaq::detdataformats::DAQEthHeader daq_hdr{};
    std::memcpy(&daq_hdr, backend.last_packet.data() + wibeth::kPacketHeaderBytes, sizeof(daq_hdr));
    BOOST_CHECK_EQUAL(daq_hdr.det_id, expected_det_ids[q]);
    BOOST_CHECK_EQUAL(daq_hdr.seq_id, 17U);
  }
}

BOOST_AUTO_TEST_CASE(ConfigurePortDiscoveryAndQueueLimitFailuresRollBackEal)
{
  // n_ports == 0.
  {
    SampleDalFixture fx;
    FakeDpdkState state;
    state.ports.clear();
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
    BOOST_CHECK(!core.eal_initialized());
  }

  // Requested tx_pcie not present among available ports.
  {
    SampleDalFixture fx;
    fx.conf.tx_pcie = "0000:ff:00.0";
    FakeDpdkState state;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
  }

  // rte_eth_dev_info_get failure.
  {
    SampleDalFixture fx;
    FakeDpdkState state;
    state.fail_dev_info = true;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
  }

  // n_queues > dev_info.max_tx_queues (3 streams, max_tx_queues == 2).
  {
    SampleDalFixture fx;
    FakeDpdkState state;
    state.ports[0].max_tx_queues = 2;
    FakeCallbackRegistry registry;
    test_core_t core("sender-0", FakeDpdkRuntime{ &state });
    core.init<FakeModuleDal>(fx.cfg);
    BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);
    BOOST_CHECK_EQUAL(state.init_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
    BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
  }
}

BOOST_AUTO_TEST_CASE(ConfigureRollbackOnMempoolFailureFreesEarlierPoolsBeforeFinishEalAndRecovers)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  state.fail_mempool_on_call = 1; // queue 0 succeeds, queue 1 throws
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);

  BOOST_CHECK_EQUAL(state.live_pools, 0);
  BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
  BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
  BOOST_CHECK(!core.eal_initialized());
  BOOST_CHECK(!core.port_configured());
  BOOST_CHECK_LT(event_index(state.events, "free_pool:dfs_tx_pool_0"), event_index(state.events, "finish_eal"));

  // Clearing the fault allows a clean re-configure on the same instance.
  state.fail_mempool_on_call = -1;
  BOOST_CHECK_NO_THROW(core.configure(registry));
  BOOST_CHECK(core.eal_initialized());
  BOOST_CHECK(core.port_configured());
  BOOST_CHECK_EQUAL(state.live_pools, 3);
}

BOOST_AUTO_TEST_CASE(ConfigureRollbackOnIfaceInitFailureFreesPoolsWithoutPortCloseAndRecovers)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  state.fail_iface_init = true;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);

  BOOST_CHECK_EQUAL(state.stop_close_calls, 0);
  BOOST_CHECK_EQUAL(state.live_pools, 0);
  BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
  const auto eal_end = event_index(state.events, "finish_eal");
  BOOST_CHECK_LT(event_index(state.events, "free_pool:dfs_tx_pool_0"), eal_end);
  BOOST_CHECK_LT(event_index(state.events, "free_pool:dfs_tx_pool_1"), eal_end);
  BOOST_CHECK_LT(event_index(state.events, "free_pool:dfs_tx_pool_2"), eal_end);

  state.fail_iface_init = false;
  BOOST_CHECK_NO_THROW(core.configure(registry));
  BOOST_CHECK(core.port_configured());
  BOOST_CHECK_EQUAL(state.live_pools, 3);
}

BOOST_AUTO_TEST_CASE(ConfigureRollbackOnMacAddrFailureClosesPortThenFreesPoolsThenFinishesEalAndRecovers)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  state.fail_macaddr = true;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  BOOST_CHECK_THROW(core.configure(registry), dunedaq::datahandlinglibs::InitializationError);

  BOOST_CHECK_EQUAL(state.stop_close_calls, 1);
  BOOST_CHECK_EQUAL(state.live_pools, 0);
  BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);
  BOOST_CHECK(!core.port_configured());
  BOOST_CHECK(!core.eal_initialized());

  const auto close_idx = event_index(state.events, "eth_dev_stop_and_close");
  const auto pool0_idx = event_index(state.events, "free_pool:dfs_tx_pool_0");
  const auto finish_idx = event_index(state.events, "finish_eal");
  BOOST_CHECK_LT(close_idx, pool0_idx);
  BOOST_CHECK_LT(pool0_idx, finish_idx);

  state.fail_macaddr = false;
  BOOST_CHECK_NO_THROW(core.configure(registry));
  BOOST_CHECK(core.port_configured());
  BOOST_CHECK_EQUAL(state.live_pools, 3);
}

BOOST_AUTO_TEST_CASE(ConfigureRollbackOnMidLoopRegisterFailureTombstonesArmedStreamAndRecoversWithoutDuplicateRegistration)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  FakeCallbackRegistry registry;
  registry.throw_on_uid = "cb-101"; // stream 0 registers & arms, stream 1 throws

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);
  BOOST_CHECK_THROW(core.configure(registry), std::runtime_error);

  // Stream 0 was already armed before stream 1 threw; rollback must tombstone it
  // and release port, pools, and EAL in order.
  BOOST_CHECK(!core.transmitters()[0]->armed());
  BOOST_CHECK(!core.transmitters()[1]->armed());
  BOOST_CHECK(!core.transmitters()[2]->armed());
  BOOST_CHECK(!core.port_configured());
  BOOST_CHECK(!core.eal_initialized());
  BOOST_CHECK_EQUAL(state.stop_close_calls, 1);
  BOOST_CHECK_EQUAL(state.live_pools, 0);
  BOOST_CHECK_EQUAL(state.finish_eal_calls, 1);

  // Clear the fault and re-configure: stream 0 must re-arm without attempting a
  // duplicate registration, while streams 1 and 2 register freshly.
  registry.throw_on_uid.clear();
  BOOST_CHECK_NO_THROW(core.configure(registry));
  BOOST_CHECK_EQUAL(registry.callbacks.size(), 3);
  BOOST_CHECK_EQUAL(registry.rejected_duplicates, 0);
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(tx->armed());
  }

  core.start();
  registry.callbacks.at("cb-100")(FakePayload{});
  registry.callbacks.at("cb-101")(FakePayload{});
  registry.callbacks.at("cb-102")(FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").sent, 1);
  BOOST_CHECK_EQUAL(core.transmitters()[1]->snapshot_stats().at("cb-101").sent, 1);
  BOOST_CHECK_EQUAL(core.transmitters()[2]->snapshot_stats().at("cb-102").sent, 1);
}

BOOST_AUTO_TEST_CASE(ConfigureScrapConfigureReArmsWithoutDuplicateRegistration)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);

  // First cycle.
  core.configure(registry);
  BOOST_REQUIRE_EQUAL(registry.callbacks.size(), 3);
  BOOST_CHECK_EQUAL(registry.register_attempts, 3);
  core.start();
  registry.callbacks.at("cb-100")(FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").sent, 1);

  // Scrap tears down port -> pools -> EAL and tombstones callbacks.
  core.scrap();
  const auto close_idx = event_index(state.events, "eth_dev_stop_and_close");
  const auto pool0_idx = event_index(state.events, "free_pool:dfs_tx_pool_0");
  const auto finish_idx = event_index(state.events, "finish_eal");
  BOOST_CHECK_LT(close_idx, pool0_idx);
  BOOST_CHECK_LT(pool0_idx, finish_idx);

  // Second configure re-creates EAL/port/pools and re-arms without re-registering.
  core.configure(registry);
  BOOST_CHECK_EQUAL(state.init_eal_calls, 2);
  BOOST_CHECK_EQUAL(state.iface_init_calls, 2);
  BOOST_CHECK_EQUAL(state.live_pools, 3);
  BOOST_CHECK_EQUAL(registry.register_attempts, 3);
  BOOST_CHECK_EQUAL(registry.rejected_duplicates, 0);
  for (const auto& tx : core.transmitters()) {
    BOOST_CHECK(tx->armed());
  }

  core.start();
  registry.callbacks.at("cb-100")(FakePayload{});
  BOOST_CHECK_EQUAL(core.transmitters()[0]->snapshot_stats().at("cb-100").sent, 2);
}

BOOST_AUTO_TEST_CASE(ConfigureWhenPortAlreadyConfiguredSkipsEalAndPortReSetup)
{
  SampleDalFixture fx;
  FakeDpdkState state;
  FakeCallbackRegistry registry;

  test_core_t core("sender-0", FakeDpdkRuntime{ &state });
  core.init<FakeModuleDal>(fx.cfg);

  core.configure(registry);
  BOOST_CHECK_EQUAL(state.init_eal_calls, 1);
  BOOST_CHECK_EQUAL(state.create_mempool_calls, 3);
  BOOST_CHECK_EQUAL(state.iface_init_calls, 1);

  // Second configure while port_configured() is already true must skip EAL/port setup.
  core.configure(registry);
  BOOST_CHECK_EQUAL(state.init_eal_calls, 1);
  BOOST_CHECK_EQUAL(state.create_mempool_calls, 3);
  BOOST_CHECK_EQUAL(state.iface_init_calls, 1);
  BOOST_CHECK_EQUAL(registry.rejected_duplicates, 0);
}

BOOST_AUTO_TEST_SUITE_END()
