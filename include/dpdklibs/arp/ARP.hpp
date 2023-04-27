/**
 * @file ARP.hpp ARP helpers
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_ARP_ARP_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_ARP_ARP_HPP_

#include <cstdint>
#include <rte_mbuf.h>
#include "dpdklibs/udp/Utils.hpp"

namespace dunedaq {
namespace dpdklibs {
namespace arp {

// RARP and ARP opcodes
enum {
  ARP_REQUEST    = 1,
  ARP_REPLY      = 2,
  RARP_REQUEST   = 3, 
  RARP_REPLY     = 4,
  GRATUITOUS_ARP = 5
};

// Queue flags
enum {
  CLEAR_FAST_ALLOC_FLAG = 0x00000001, // < Clear the TX fast alloc flag
  DO_TX_FLUSH           = 0x00000002  // Do a TX Flush by sending all of the pkts in the queue
};

// ethSwap(u16_t * to, u16_t * from) - Swap two 16 bit values 
inline void
uint16Swap(void *t, void *f) {
  uint16_t *d = (uint16_t *)t;
  uint16_t *s = (uint16_t *)f;
  uint16_t v;
  v = *d; 
  *d = *s; 
  *s = v;
}

// ethAddrSwap( u16_t * to, u16_t * from ) - Swap two ethernet addresses
inline void
ethAddrSwap(void *t, void *f) {
  uint16_t *d = (uint16_t *)t;
  uint16_t *s = (uint16_t *)f;
  uint16Swap(d++, s++);
  uint16Swap(d++, s++);
  uint16Swap(d, s);
}

// inetAddrSwap( void * t, void * f ) - Swap two IPv4 addresses
inline void
inetAddrSwap(void *t, void *f) {
  uint32_t *d = (uint32_t *)t;
  uint32_t *s = (uint32_t *)f;
  uint32_t v;
  v = *d; 
  *d = *s; 
  *s = v;
}

/* inetAddrCopy( void * t, void * f ) - Copy IPv4 address */
inline void
inetAddrCopy(void *t, void *f) {
	uint32_t *d = (uint32_t *)t;
	uint32_t *s = (uint32_t *)f;

	*d = *s;
}

// Send GARP
void pktgen_send_garp(struct rte_mbuf *m, uint32_t port_id, rte_be32_t binary_ip_address);

// Reply to ARP
void pktgen_process_arp(struct rte_mbuf *m, uint32_t pid, rte_be32_t binary_ip_address);

} // namespace arp
} // namespace dpdklibs
} // namespace dunedaq

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_ARP_ARP_HPP_
