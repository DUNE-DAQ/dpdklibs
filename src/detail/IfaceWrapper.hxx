#include <time.h>
#include <rte_arp.h>
#include <rte_ethdev.h>
#include "dpdklibs/udp/IPV4UDPPacket.hpp"

namespace dunedaq {
namespace dpdklibs {

int 
IfaceWrapper::rx_runner(void *arg __rte_unused) {

  // Timespec for opportunistic sleep. Nanoseconds configured in conf.
	struct timespec sleep_request = { 0, (long)m_lcore_sleep_ns };

  //bool once = true; // One shot action variable.
  uint16_t iface = m_iface_id;

  const uint16_t lid = rte_lcore_id();
  auto queues = m_rx_core_map[lid];

  if (rte_eth_dev_socket_id(iface) >= 0 && rte_eth_dev_socket_id(iface) != (int)rte_socket_id()) {
    TLOG() << "WARNING, iface " << iface << " is on remote NUMA node to polling thread! "
           << "Performance will not be optimal.";
  }

  TLOG() << "LCore RX runner on CPU[" << lid << "]: Main loop starts for iface " << iface << " !";

  std::map<int, int> nb_rx_map;
  // While loop of quit atomic member in IfaceWrapper
  while(!this->m_lcore_quit_signal.load()) {

    // Loop over assigned queues to process
    uint8_t fb_count(0);
    for (const auto& q : queues) {
      auto src_rx_q = q.first;
      auto* q_bufs = m_bufs[src_rx_q];

      // Get burst from queue
      const uint16_t nb_rx = rte_eth_rx_burst(iface, src_rx_q, q_bufs, m_burst_size);
      nb_rx_map[src_rx_q] = nb_rx;
    }


    for (const auto& q : queues) {

      auto src_rx_q = q.first;
      auto* q_bufs = m_bufs[src_rx_q];
      const uint16_t nb_rx = nb_rx_map[src_rx_q];
  
      // We got packets from burst on this queue
      if (nb_rx != 0) [[likely]] {

        // Update max burst size counter of this queue
        m_max_burst_size[src_rx_q] = std::max(nb_rx, m_max_burst_size[src_rx_q].load());

        // -------
	      // Iterate on burst packets
        for (int i_b=0; i_b<nb_rx; ++i_b) {

          // Check if packet is segmented. Implement support for it if needed.
          //if (q_bufs[i_b]->nb_segs > 1) [[unlikely]] {
          //  TLOG_DEBUG(10) << "It appears a packet is spread across more than one receiving buffer;" 
          //                 << " there's currently no logic in this program to handle this";
          //}

          // Check packet type, decide their fate: ignore unexpected ones, FIXME: monitor occurrences
          auto pkt_type = q_bufs[i_b]->packet_type;
          // Handle non IPV4 frames.
          if (not RTE_ETH_IS_IPV4_HDR(pkt_type)) [[unlikely]] {
            //TLOG_DEBUG(10) << "Non-Ethernet packet type: " << (unsigned)pkt_type << " original: " << pkt_type;
            if (pkt_type == RTE_PTYPE_L2_ETHER_ARP) {
              //TLOG() << "Unexpected: Should handle an ARP request from lcore=" << lid << " rx_q=" << src_rx_q << "! Flow should be steered to dedicated RX Queue.";
            } else if (pkt_type == RTE_PTYPE_L2_ETHER_LLDP) {
              //TLOG_DEBUG(10) << "TODO: Handle LLDP packet!";
            } else {
              //TLOG_DEBUG(10) << "Unidentified! Dumping...";
              //rte_pktmbuf_dump(stdout, q_bufs[i_b], m_bufs[src_rx_q][i_b]->pkt_len);
            }
            ++m_num_unhandled_non_ipv4[lid];
            continue;
          }

          // Check if frame is non UDP: in that case, ignore it.
          if ((pkt_type & RTE_PTYPE_L4_MASK) != RTE_PTYPE_L4_UDP) [[unlikely]] {
            ++m_num_unhandled_non_udp[lid];
            continue; // ommit it
          }

          // Check for JUMBO frames (bigger than 1500 Bytes)
          if (q_bufs[i_b]->pkt_len > 1500) [[likely]] { // RS FIXME: do proper check on data length later

            // If flow enabled, handle the payload.
            if ( m_lcore_enable_flow.load() ) [[likely]] {
              // Get length of user payload. (Ethernet headers excluded.)
              struct udp::ipv4_udp_packet_hdr* udp_packet = rte_pktmbuf_mtod(q_bufs[i_b], struct udp::ipv4_udp_packet_hdr*);
              char* message = udp::get_udp_payload(q_bufs[i_b]);
              std::size_t udp_payload_len = udp::get_payload_size_udp_hdr(&udp_packet->udp_hdr);

              if ( m_strict_parsing ) { // all sources maintain DAQ protocol
                parse_udp_payload(src_rx_q, message, udp_payload_len);
              } else { // avoid size checks and scattering
                passthrough_udp_payload(src_rx_q, message, udp_payload_len);
              }
            }

            // Update metrics of queue: frame and Byte counters
            ++m_num_frames_rxq[src_rx_q];
            std::size_t data_len = q_bufs[i_b]->data_len;
            m_num_bytes_rxq[src_rx_q] += data_len;
          } else {
            ++m_num_unhandled_non_jumbo_udp[lid];
          }
        }

        // Bulk free of mbufs
        rte_pktmbuf_free_bulk(q_bufs, nb_rx);

      } // per burst
      // -----------

      // Full burst counter
      if (nb_rx == m_burst_size) {
        ++fb_count;
        ++m_num_full_bursts[src_rx_q];
      }
    } // per queue

    // If no full buffers in burst...
    if (!fb_count) {
      if (m_lcore_sleep_ns) {
        // Sleep n nanoseconds... (value from config, timespec initialized in lcore first lines)
        /*int response =*/ nanosleep(&sleep_request, nullptr);
      }
    }

  } // main while(quit) loop
 
  TLOG() << "LCore RX runner on CPU[" << lid << "] returned.";
  return 0;
}


int 
IfaceWrapper::arp_response_runner(void *arg __rte_unused) {

  // Timespec for opportunistic sleep. Nanoseconds configured in conf.
  struct timespec sleep_request = { 0, (long)900000 };

  //bool once = true; // One shot action variable.
  uint16_t iface = m_iface_id;

  const uint16_t lid = rte_lcore_id();
  unsigned arp_rx_queue = m_arp_rx_queue;

  TLOG() << "LCore ARP responder on CPU[" << lid << "]: Main loop starts for iface " << iface << " rx queue: " << arp_rx_queue;

  // While loop of quit atomic member in IfaceWrapper
  while(!this->m_lcore_quit_signal.load()) {

    const uint16_t nb_rx = rte_eth_rx_burst(iface, arp_rx_queue, m_arp_bufs[arp_rx_queue], m_burst_size);

    // We got packets from burst on this queue
    if (nb_rx != 0) {
      // Iterate on burst packets
      for (int i_b=0; i_b<nb_rx; ++i_b) {

        // Check packet type, ommit/drop unexpected ones.
        auto pkt_type = m_arp_bufs[arp_rx_queue][i_b]->packet_type;
        //// Handle non IPV4 packets
        if (not RTE_ETH_IS_IPV4_HDR(pkt_type)) {
          //TLOG_DEBUG(10) << "Non-Ethernet packet type: " << (unsigned)pkt_type << " original: " << pkt_type;
          if (pkt_type == RTE_PTYPE_L2_ETHER_ARP) {
            TLOG_DEBUG(10) << "Handling ARP request";
            struct rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(m_arp_bufs[arp_rx_queue][i_b], struct rte_ether_hdr *);
            struct rte_arp_hdr* arp_hdr = (struct rte_arp_hdr *)(eth_hdr + 1);

            std::string srcaddr = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(arp_hdr->arp_data.arp_sip)));
            TLOG_DEBUG(10) << "SRC IP: " << srcaddr;
            std::string dstaddr = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(arp_hdr->arp_data.arp_tip)));
            TLOG_DEBUG(10) << "DEST IP: " << dstaddr;

            for( const auto& ip_addr_bin : m_ip_addr_bin) {
              std::string localaddr = dunedaq::dpdklibs::udp::get_ipv4_decimal_addr_str(dunedaq::dpdklibs::udp::ip_address_binary_to_dotdecimal(rte_be_to_cpu_32(ip_addr_bin)));
              TLOG_DEBUG(10) << "LOCAL IP: " << localaddr;
            }


            if (std::find(m_ip_addr_bin.begin(), m_ip_addr_bin.end(), arp_hdr->arp_data.arp_tip) != m_ip_addr_bin.end()) {
              arp::pktgen_process_arp(m_arp_bufs[arp_rx_queue][i_b], m_iface_id, arp_hdr->arp_data.arp_tip);
            } else {
              TLOG_DEBUG(10) << "I'm not the ARP target";
            }
          } else if (pkt_type == RTE_PTYPE_L2_ETHER_LLDP) {
            //TLOG_DEBUG(10) << "TODO: Handle LLDP packet!";
          } else {
            //TLOG_DEBUG(10) << "Unidentified! Dumping...";
            //rte_pktmbuf_dump(stdout, m_arp_bufs[arp_rx_queue][i_b], m_bufs[src_rx_q][i_b]->pkt_len);
          }
          continue;
        }
      }

      // Bulk free of mbufs
      rte_pktmbuf_free_bulk(m_arp_bufs[arp_rx_queue], nb_rx);
      
    } // per burst

    // If no full buffers in burst...
    if (m_lcore_sleep_ns) {
      // Sleep n nanoseconds... (value from config, timespec initialized in lcore first lines)
      /*int response =*/ nanosleep(&sleep_request, nullptr);
    }

  } // main while(quit) loop
 
  TLOG() << "LCore ARP responder on CPU[" << lid << "] returned.";
  return 0;
}

} // namespace dpdklibs
} // namespace dunedaq
