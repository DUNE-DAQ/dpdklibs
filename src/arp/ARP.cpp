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

#include <iostream>
#include <sstream>
#include <iomanip>

#include "dpdklibs/arp/ARP.hpp"

namespace dunedaq {
namespace dpdklibs {
namespace arp {

void 
pktgen_send_garp(struct rte_mbuf *m, uint32_t port_id, rte_be32_t binary_ip_address)
{
  struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  struct rte_arp_hdr *arp = (struct rte_arp_hdr *)&eth[1];

  /* src and dest addr */
	memset(&eth->dst_addr, 0xFF, 6);
  // MAC addr of port
 	struct rte_ether_addr mac_addr;
  rte_eth_macaddr_get(port_id, &mac_addr);
	rte_ether_addr_copy(&mac_addr, &eth->src_addr);
  // Set ETH type
	eth->ether_type = htons(RTE_ETHER_TYPE_ARP);

  
  memset(arp, 0, sizeof(struct rte_arp_hdr));
  rte_memcpy(&arp->arp_data.arp_sha, &mac_addr, 6);
  
  uint32_t addr = htonl(binary_ip_address);
  inetAddrCopy(&arp->arp_data.arp_sip, &addr);

  //if (likely(type == GRATUITOUS_ARP) ) {
		rte_memcpy(&arp->arp_data.arp_tha, &mac_addr, 6);
		inetAddrCopy(&arp->arp_data.arp_tip, &addr);
  //} else {
  //  memset(&arp->arp_data.arp_tha, 0, 6);
  //  addr = htonl(pkt->ip_dst_addr.addr.ipv4.s_addr);
  //  inetAddrCopy(&arp->arp_data.arp_tip, &addr);
  //}

  /* Fill in the rest of the ARP packet header */
	arp->arp_hardware = htons(RTE_ARP_HRD_ETHER);
	arp->arp_protocol = htons(RTE_ETHER_TYPE_IPV4);
	arp->arp_hlen     = 6;
	arp->arp_plen     = 4;
	arp->arp_opcode   = htons(RTE_ARP_OP_REQUEST);

  m->pkt_len  = 60;
	m->data_len = 60;

  struct rte_mbuf *arp_tx_mbuf[1];
  arp_tx_mbuf[0] = m;
  rte_eth_tx_burst(port_id, 0, arp_tx_mbuf, 1);
  //printf("Sending ARP reply\n");
}


inline void
hex_digits_to_stream(std::ostringstream& ostrs, int value, char separator = ':', char fill = '0', int digits = 2) {
  ostrs << std::setfill(fill) << std::setw(digits) << std::hex << value << std::dec << separator;
}


void
pktgen_process_arp(struct rte_mbuf *m, uint32_t port_id, rte_be32_t binary_ip_address)
{
  /*port_info_t   *info = &pktgen.info[port_id];*/
  /*pkt_seq_t     *pkt;*/
  struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
  struct rte_arp_hdr *arp = (struct rte_arp_hdr *)&eth[1];

  /* Process all ARP requests if they are for us. */
  //std::cout << "ARP code: " << (unsigned)arp->arp_opcode << '\n';

  if (arp->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)) {
    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
  //}

  //if (arp->arp_opcode == htons(ARP_REQUEST) ) {

    /* Grab the source MAC addresses */
    struct rte_ether_addr mac_addr;
    rte_eth_macaddr_get(port_id, &mac_addr);

    std::ostringstream ostrs;
    ostrs << "dst mac addr: ";
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[0]);
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[1]);
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[2]);
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[3]);
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[4]);
    hex_digits_to_stream(ostrs, (int)mac_addr.addr_bytes[5], '\n');
    //std::cout << "DST MAC: " << ostrs.str();

    /* ARP request not for this interface. */
    //if (likely(pkt != NULL) ) {

    //if (unlikely(arp->arp_data.arp_tip == mac_addr)) { //binary_ip_address)) {

      

      printf("ARP Received %i, local %i - I'm the target\n", ntohl(arp->arp_data.arp_tip), ntohl(binary_ip_address));

      std::string srcaddr = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(arp->arp_data.arp_sip)));
      //std::cout << "SRC IP: " << srcaddr << '\n';
      std::string dstaddr = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(arp->arp_data.arp_tip)));
      //std::cout << "DEST IP: " << dstaddr << '\n';

      /* Swap the two MAC addresses */
      ethAddrSwap(&arp->arp_data.arp_sha, &arp->arp_data.arp_tha);

      /* Swap the two IP addresses */
      inetAddrSwap(&arp->arp_data.arp_tip, &arp->arp_data.arp_sip);

      /* Set the packet to ARP reply */
      arp->arp_opcode = htons(RTE_ARP_OP_REPLY);

      /* Swap the MAC addresses */
      ethAddrSwap(&eth->dst_addr, &eth->src_addr);
      //ethAddrSwap(&eth->dst_addr, &eth->src_addr);

      /* Copy in the MAC address for the reply. */
      rte_memcpy(&arp->arp_data.arp_sha, &mac_addr, 6);
      // rte_memcpy(&eth->src_addr, &mac_addr, 6);
      rte_memcpy(&eth->src_addr, &mac_addr, 6);

      //pktgen_send_mbuf(m, port_id, 0);

      struct rte_mbuf *arp_tx_mbuf[1];
      arp_tx_mbuf[0] = m;

      rte_eth_tx_burst(port_id, 0, arp_tx_mbuf, 1);
      printf("Sending ARP reply\n");
      /* Flush all of the packets in the queue. */
      //pktgen_set_q_flags(info, 0, DO_TX_FLUSH);

      /* No need to free mbuf as it was reused */
      return;
    //} else {
    //  printf("ARP Received %i, local %i - I'm not the target\n", ntohl(arp->arp_data.arp_tip), ntohl(binary_ip_address));
    //}
  }
}

} // namespace arp
} // namespace dpdklibs
} // namespace dunedaq
