/**
 * @file DpdkPortRuntime.hpp
 *
 * Production DPDK runtime seam used by DPDKFakeDataSender to instantiate
 * sender::FakeSenderCore against EAL, rte_ethdev, and rte_mempool.
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_DPDKPORTRUNTIME_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_DPDKPORTRUNTIME_HPP_

#include "dpdklibs/DpdkMempool.hpp"
#include "dpdklibs/EALSetup.hpp"
#include "dpdklibs/sender/DpdkTxBackend.hpp"
#include "dpdklibs/sender/FakeSenderUtils.hpp"
#include "dpdklibs/RTEIfaceSetup.hpp"

#include <rte_ethdev.h>
#include <rte_ether.h>

#include <unistd.h>

#include <string>
#include <vector>

namespace dunedaq::dpdklibs::sender {

struct DpdkPortRuntime
{
  using pool_handle_t = unique_mempool;
  using dpdk_id_t = detail::dpdk_id_t;

  pid_t get_pid() const { return ::getpid(); }

  void init_eal(const std::vector<std::string>& args) { ealutils::init_eal(args); }

  void finish_eal() { ealutils::finish_eal(); }

  dpdk_id_t eth_dev_count_avail() const { return rte_eth_dev_count_avail(); }

  std::string get_iface_pci_str(dpdk_id_t port) const { return ifaceutils::get_iface_pci_str(port); }

  bool eth_dev_info_get(dpdk_id_t port, dpdk_id_t& max_tx_queues) const
  {
    rte_eth_dev_info dev_info{};
    if (rte_eth_dev_info_get(port, &dev_info) != 0) {
      return false;
    }
    max_tx_queues = dev_info.max_tx_queues;
    return true;
  }

  int eth_dev_socket_id(dpdk_id_t port) const { return rte_eth_dev_socket_id(port); }

  pool_handle_t create_mempool(const std::string& name,
                               int num_bufs,
                               int mbuf_cache_size,
                               int data_room_size,
                               int socket_id)
  {
    return take_mempool_ownership(
      ealutils::get_mempool(name, num_bufs, mbuf_cache_size, data_room_size, socket_id));
  }

  void iface_init_tx_only(dpdk_id_t port, dpdk_id_t n_tx_queues, dpdk_id_t tx_ring_size)
  {
    // TX only: no RX queue.  iface_init reads the pool map for RX queues only;
    // the map stays empty.
    BorrowedPoolMap no_rx_pools;
    ealutils::iface_init(port, 0, n_tx_queues, 0, tx_ring_size, no_rx_pools.get(), false, false, false);
  }

  bool eth_macaddr_get(dpdk_id_t port, rte_ether_addr& addr) const
  {
    return rte_eth_macaddr_get(port, &addr) == 0;
  }

  void eth_dev_stop_and_close(dpdk_id_t port)
  {
    rte_eth_dev_stop(port);
    rte_eth_dev_close(port);
  }

  DpdkTxBackend make_tx_backend(const pool_handle_t& pool, dpdk_id_t port, dpdk_id_t queue) const
  {
    return DpdkTxBackend{ pool.get(), port, queue };
  }
};

} // namespace dunedaq::dpdklibs::sender

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_SENDER_DPDKPORTRUNTIME_HPP_
