/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2017 Mellanox Technologies, Ltd
 */

#include "dpdklibs/FlowControl.hpp"

namespace dunedaq {
namespace dpdklibs {

/**
 * create a flow rule that sends packets with matching src and dest ip
 * to selected queue.
 *
 * @param port_id
 *   The selected port.
 * @param rx_q
 *   The selected target queue.
 * @param src_ip
 *   The src ip value to match the input packet.
 * @param src_mask
 *   The mask to apply to the src ip.
 * @param dest_ip
 *   The dest ip value to match the input packet.
 * @param dest_mask
 *   The mask to apply to the dest ip.
 * @param[out] error
 *   Perform verbose error reporting if not NULL.
 *
 * @return
 *   A flow if the rule could be created else return NULL.
 */

/* Function responsible for creating the flow rule. 8< */
struct rte_flow *
generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
                   uint32_t src_ip, uint32_t src_mask,
                   uint32_t dest_ip, uint32_t dest_mask,
                   struct rte_flow_error *error)
{
  // Declaring structs being used.
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[MAX_PATTERN_NUM];
  struct rte_flow_action action[MAX_ACTION_NUM];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = { .index = rx_q };
  struct rte_flow_item_ipv4 ip_spec;
  struct rte_flow_item_ipv4 ip_mask;
  int res;
  
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));
  
  // Set the rule attribute, only ingress packets will be checked.
  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;
  attr.egress = 0;
  attr.priority = 0;
  
  /*
   * create the action sequence.
   * one action only,  move packet to queue
   */
  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;
  
  /*
   * set the first level of the pattern (ETH).
   * since in this example we just want to get the
   * ipv4 we set this level to allow all.
   */
  
  // Set this level to allow all.
  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  
  /*
   * setting the second level of the pattern (IP).
   * in this example this is the level we care about
   * so we set it according to the parameters.
   */
  
  // Setting the second level of the pattern.
  memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
  memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
  
  
  // Cast to small endian
  const rte_le32_t src_addr_ser = *(reinterpret_cast<rte_le32_t *>(&src_ip));
  
  //ip_spec.hdr.dst_addr = htonl(dest_ip);
  // Convertion to big endian
  ip_spec.hdr.dst_addr = rte_cpu_to_be_32(src_addr_ser);
  
  ip_mask.hdr.dst_addr = *(reinterpret_cast<uint32_t *>(&dest_mask)); // TODO why endian convertion for ip but not for the mask?
  
  const rte_le32_t dst_addr_ser = *(reinterpret_cast<rte_le32_t *>(&dest_ip));
  //ip_spec.hdr.src_addr = htonl(src_ip);
  ip_spec.hdr.src_addr = rte_cpu_to_be_32(dst_addr_ser);
  
  ip_mask.hdr.src_addr = *(reinterpret_cast<uint32_t *>(&src_mask)); // TODO why endian convertion for ip but not for the mask?
  
  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[1].spec = &ip_spec;
  pattern[1].mask = &ip_mask;
  
  // The final level must be always type end.
  pattern[2].type = RTE_FLOW_ITEM_TYPE_END;
  
  // Validate the rule and create it.
  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (not res) {
  	flow = rte_flow_create(port_id, &attr, pattern, action, error);
  }
  
  return flow;
}

// Droppign all the traffic that did pass a flow with higher priority
struct rte_flow *
generate_drop_flow(uint16_t port_id, struct rte_flow_error *error)
{
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[MAX_PATTERN_NUM];
  struct rte_flow_action action[MAX_ACTION_NUM];
  struct rte_flow *flow = NULL;
  int res;
  
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));
  
  // Set the rule attribute, only ingress packets will be checked.
  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;
  attr.egress = 0;
  attr.priority = 1; // TODO the higher the lower?
  
  /*
   * create the action sequence.
   * one action only,  Drop the packet
   */
  action[0].type = RTE_FLOW_ACTION_TYPE_DROP;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;
  
  /*
   * set the first level of the pattern (ETH).
   * since in this example we just want to get the
   * ipv4 we set this level to allow all.
   */
  
  // Set this level to allow all.
  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  
  /*
   * setting the second level of the pattern (IP).
   * in this example this is the level we care about
   * so we set it according to the parameters.
   */
  
  // The final level must be always type end.
  pattern[1].type = RTE_FLOW_ITEM_TYPE_END;
  
  // Validate the rule and create it.
  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (not res) {
  	flow = rte_flow_create(port_id, &attr, pattern, action, error);
  }
  
  return flow;
}

} // namespace dpdklibs
} // namespace dunedaq