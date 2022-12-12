/**
 * @file ARP.cpp ARP helpers implementation
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include <arpa/inet.h>
#include <rte_arp.h>
#include <rte_ethdev.h>

#include "dpdklibs/arp/ARP.hpp"

namespace dunedaq {
namespace dpdklibs {
namespace arp {

/**************************************************************************/ /**
                                                                              *
                                                                              * pktgen_process_arp - Handle a ARP
                                                                              * request input packet and send a
                                                                              * response.
                                                                              *
                                                                              * DESCRIPTION
                                                                              * Handle a ARP request input packet and
                                                                              * send a response if required.
                                                                              *
                                                                              * RETURNS: N/A
                                                                              *
                                                                              * SEE ALSO:
                                                                              */
void
pktgen_process_arp(struct rte_mbuf* m, uint32_t port_id, rte_be32_t binary_ip_address)
{
  /*port_info_t   *info = &pktgen.info[port_id];*/
  /*pkt_seq_t     *pkt;*/
  struct rte_ether_hdr* eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr*);
  struct rte_arp_hdr* arp = (struct rte_arp_hdr*)&eth[1];

  /* Process all ARP requests if they are for us. */
  if (arp->arp_opcode == htons(ARP_REQUEST)) {

    /* Grab the source MAC addresses */
    struct rte_ether_addr mac_addr;
    rte_eth_macaddr_get(port_id, &mac_addr);

    /* ARP request not for this interface. */
    // if (likely(pkt != NULL) ) {

    if (unlikely(arp->arp_data.arp_tip == binary_ip_address)) {
      printf("ARP Received %i, local %i - I'm the target\n", ntohl(arp->arp_data.arp_tip), ntohl(binary_ip_address));

      /* Swap the two MAC addresses */
      ethAddrSwap(&arp->arp_data.arp_sha, &arp->arp_data.arp_tha);

      /* Swap the two IP addresses */
      inetAddrSwap(&arp->arp_data.arp_tip, &arp->arp_data.arp_sip);

      /* Set the packet to ARP reply */
      arp->arp_opcode = htons(ARP_REPLY);

      /* Swap the MAC addresses */
      ethAddrSwap(&eth->dst_addr, &eth->src_addr);
      // ethAddrSwap(&eth->dst_addr, &eth->src_addr);

      /* Copy in the MAC address for the reply. */
      rte_memcpy(&arp->arp_data.arp_sha, &mac_addr, 6);
      // rte_memcpy(&eth->src_addr, &mac_addr, 6);
      rte_memcpy(&eth->src_addr, &mac_addr, 6);

      // pktgen_send_mbuf(m, port_id, 0);

      struct rte_mbuf* arp_tx_mbuf[1];
      arp_tx_mbuf[0] = m;

      rte_eth_tx_burst(port_id, 0, arp_tx_mbuf, 1);
      printf("Sending ARP reply\n");
      /* Flush all of the packets in the queue. */
      // pktgen_set_q_flags(info, 0, DO_TX_FLUSH);

      /* No need to free mbuf as it was reused */
      return;
    } else {
      printf(
        "ARP Received %i, local %i - I'm not the target\n", ntohl(arp->arp_data.arp_tip), ntohl(binary_ip_address));
    }
  }
}

} // namespace arp
} // namespace dpdklibs
} // namespace dunedaq
