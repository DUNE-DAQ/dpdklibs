
#include <time.h>

namespace dunedaq {
namespace dpdklibs {

int 
IfaceWrapper::rx_runner(void *arg __rte_unused) {

  // Timespec for opportunistic sleep. Nanoseconds configured in conf.
  struct timespec sleep_request = { 0, (long)m_lcore_sleep_ns };

  bool once = true; // One shot action variable.
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
      if (nb_rx != 0) {

        m_max_burst_size[src_rx_q] = std::max(nb_rx, m_max_burst_size[src_rx_q].load());
        // -------
	      // Iterate on burst packets
        for (int i_b=0; i_b<nb_rx; ++i_b) {

          // Check if packet is segmented. Implement support for it if needed.
          if (q_bufs[i_b]->nb_segs > 1) {
	          //TLOG_DEBUG(10) << "It appears a packet is spread across more than one receiving buffer;" 
            //               << " there's currently no logic in this program to handle this";
	        }

          // Check packet type, ommit/drop unexpected ones.
          auto pkt_type = q_bufs[i_b]->packet_type;
          //// Handle non IPV4 packets
          if (not RTE_ETH_IS_IPV4_HDR(pkt_type)) {
            //TLOG_DEBUG(10) << "Non-Ethernet packet type: " << (unsigned)pkt_type << " original: " << pkt_type;
            if (pkt_type == RTE_PTYPE_L2_ETHER_ARP) {
              //TLOG_DEBUG(10) << "TODO: Handle ARP request!";
            } else if (pkt_type == RTE_PTYPE_L2_ETHER_LLDP) {
              //TLOG_DEBUG(10) << "TODO: Handle LLDP packet!";
            } else {
              //TLOG_DEBUG(10) << "Unidentified! Dumping...";
              //rte_pktmbuf_dump(stdout, q_bufs[i_b], m_bufs[src_rx_q][i_b]->pkt_len);
            }
            continue;
          }

          // Check for UDP frames
          //if (pkt_type == RTE_PTYPE_L4_UDP) { // RS FIXME: doesn't work. Why? What is the PKT_TYPE in our ETH frames?
          // Check for JUMBO frames
          if (q_bufs[i_b]->pkt_len > 7000) { // RS FIXME: do proper check on data length later
            // Handle them!
            std::size_t data_len = q_bufs[i_b]->data_len;

            if ( m_lcore_enable_flow.load() ) {
              char* message = udp::get_udp_payload(q_bufs[i_b]);
              handle_eth_payload(src_rx_q, message, data_len);
            }
            ++m_num_frames_rxq[src_rx_q];
            m_num_bytes_rxq[src_rx_q] += data_len;
          }
        }

        // Bulk free of mbufs
        rte_pktmbuf_free_bulk(q_bufs, nb_rx);

        // -------
        
      } // per burst

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

} // namespace dpdklibs
} // namespace dunedaq
