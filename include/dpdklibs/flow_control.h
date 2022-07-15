#ifndef _FLOW_CONTROL_H_
#define _FLOW_CONTROL_H_

#include <stdint.h>
#include <rte_flow.h>

#include "dpdklibs/ipv4_addr.h"

static constexpr uint32_t  MAX_PATTERN_NUM = 3;
static constexpr uint32_t  MAX_ACTION_NUM  = 2;

struct rte_flow *
generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
		IpAddr src_ip, IpAddr src_mask,
		IpAddr dest_ip, IpAddr dest_mask,
		struct rte_flow_error *error);

struct rte_flow *
generate_drop_flow(uint16_t port_id, struct rte_flow_error *error);

#endif  /* _FLOW_CONTROL_H_ */
