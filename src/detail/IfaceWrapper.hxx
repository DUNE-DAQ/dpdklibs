
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

  // While loop of quit atomic member in IfaceWrapper
  while(!this->m_lcore_quit_signal.load()) {

    // Loop over assigned queues to process
    for (auto q : queues) {
      uint8_t fb_count(0);
      auto src_rx_q = q.first;
      auto* q_bufs = m_bufs[src_rx_q];

      // Get burst from queue
      const uint16_t nb_rx = rte_eth_rx_burst(iface, src_rx_q, q_bufs, m_burst_size);

      // We got packets from burst on this queue
      if (nb_rx != 0) {
        // Print first packet for FYI for debugging
        /*
        if (once && q_bufs[0]->pkt_len > 7000) {
          TLOG() << "lid = " << lid;
          TLOG() << "src_rx_q = " << src_rx_q;
          TLOG() << "nb_rx = " << nb_rx;
          TLOG() << "bufs.dta_len = " << q_bufs[0]->data_len;
      	  TLOG() << "bufs.pkt_len = " << q_bufs[0]->pkt_len;
          rte_pktmbuf_dump(stdout, q_bufs[0], q_bufs[0]->pkt_len);
          std::string udp_hdr_str = udp::get_udp_header_str(m_bufs[src_rx_q][0]);
          TLOG() << "UDP Header: " << udp_hdr_str;
    	    once = false;
        }
        */

	      // Iterate on burst packets
        for (int i_b=0; i_b<nb_rx; ++i_b) {

          // Check if packet is segmented. Implement support for it if needed.
          if (m_bufs[src_rx_q][i_b]->nb_segs > 1) {
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
          if (m_bufs[src_rx_q][i_b]->pkt_len > 7000) { // RS FIXME: do proper check on data length later
            // Handle them!
            std::size_t data_len = q_bufs[i_b]->data_len;
            char* message = udp::get_udp_payload(m_bufs[src_rx_q][i_b]);
            handle_eth_payload(src_rx_q, message, data_len);
            m_num_frames_rxq[src_rx_q]++;
            m_num_bytes_rxq[src_rx_q] += data_len;
          }
        }

        // Bulk free of mbufs
        rte_pktmbuf_free_bulk(m_bufs[src_rx_q], nb_rx);

      } // per burst

      // Full burst counter
      if (nb_rx == m_burst_size) {
        ++fb_count;
      }

      // If no packets in burst...
      if (!fb_count) {
        // Sleep n nanoseconds... (value from config, timespec initialized in lcore first lines)
        /*int response =*/ nanosleep(&sleep_request, nullptr);
      }

    } // per queue

  } // main while(quit) loop
 
  TLOG() << "LCore RX runner on CPU[" << lid << "] returned.";
  return 0;
}

} // namespace dpdklibs
} // namespace dunedaq
