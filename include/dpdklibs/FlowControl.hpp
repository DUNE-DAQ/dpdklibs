/**
 * @file FlowControl.hpp Setup flow-control on NIC based on Flow API
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_FLOWCONTROL_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_FLOWCONTROL_HPP_

#include <stdint.h>
#include <rte_flow.h>

#include "dpdklibs/udp/IPV4Address.hpp"

namespace dunedaq {
namespace dpdklibs {

static constexpr uint32_t MAX_PATTERN_NUM = 4; // (shouldn't this be a configuration?)
static constexpr uint32_t MAX_ACTION_NUM  = 2;

struct rte_flow *
generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
                   udp::IpAddr src_ip, udp::IpAddr src_mask,
                   udp::IpAddr dest_ip, udp::IpAddr dest_mask,
                   struct rte_flow_error *error);

struct rte_flow *
generate_drop_flow(uint16_t port_id, struct rte_flow_error *error);

} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_FLOWCONTROL_HPP_
