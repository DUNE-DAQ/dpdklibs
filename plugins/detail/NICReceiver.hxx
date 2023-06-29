
namespace dunedaq {
namespace dpdklibs {

int 
NICReceiver::rx_runner(void *arg __rte_unused) {
  bool once = true; // One shot action variable.
  uint16_t iface = m_iface_id;

  const uint16_t lid = rte_lcore_id();
  auto queues = m_rx_core_map[lid];

  if (rte_eth_dev_socket_id(iface) >= 0 && rte_eth_dev_socket_id(iface) != (int)rte_socket_id()) {
    TLOG() << "WARNING, iface " << iface << " is on remote NUMA node to polling thread! "
           << "Performance will not be optimal.";
  }

  TLOG() << "LCore RX runner on CPU[" << lid << "]: Main loop starts for iface " << iface << " !";

  //while(!m_dpdk_quit_signal) {
  while(!ealutils::dpdk_quit_signal) {
    for (auto q : queues) {
      auto src_rx_q = q.first;
      // Get burst from queue
      const uint16_t nb_rx = rte_eth_rx_burst(iface, src_rx_q, m_bufs[src_rx_q], m_burst_size);
      if (nb_rx != 0) {
        // Print first packet for FYI
        if (once && m_bufs[src_rx_q][0]->pkt_len > 7000) {
          TLOG() << "lid = " << lid;
          TLOG() << "src_rx_q = " << src_rx_q;
          TLOG() << "nb_rx = " << nb_rx;
          TLOG() << "bufs.dta_len = " << m_bufs[src_rx_q][0]->data_len;
      	  TLOG() << "bufs.pkt_len = " << m_bufs[src_rx_q][0]->pkt_len;
          rte_pktmbuf_dump(stdout, m_bufs[src_rx_q][0], m_bufs[src_rx_q][0]->pkt_len);
          std::string udp_hdr_str = udp::get_udp_header_str(m_bufs[src_rx_q][0]);
          TLOG() << "UDP Header: " << udp_hdr_str;
    	    once = false;
        }
        

	      // Iterate on burst packets
        for (int i_b=0; i_b<nb_rx; ++i_b) {

          if (m_bufs[src_rx_q][i_b]->nb_segs > 1) {
	          TLOG() << "It appears a packet is spread across more than one receiving buffer;" 
                   << " there's currently no logic in this program to handle this";
	        }

          // Check packet type
          auto pkt_type = m_bufs[src_rx_q][i_b]->packet_type;
          //// Handle non IPV4 packets
          if (not RTE_ETH_IS_IPV4_HDR(pkt_type)) {
            TLOG() << "Non-Ethernet packet type: " << (unsigned)pkt_type << " original: " << pkt_type;
            if (pkt_type == RTE_PTYPE_L2_ETHER_ARP) {
              TLOG() << "TODO: Handle ARP request!";
            } else if (pkt_type == RTE_PTYPE_L2_ETHER_LLDP) {
              TLOG() << "TODO: Handle LLDP packet!";
            } else {
              TLOG() << "Unidentified! Dumping...";
              rte_pktmbuf_dump(stdout, m_bufs[src_rx_q][i_b], m_bufs[src_rx_q][i_b]->pkt_len);
            }
            continue;
          }

          // Check for UDP frames
          //if (pkt_type == RTE_PTYPE_L4_UDP) {
            // Check for JUMBO frames
	  bool dummy = false;
          if (m_bufs[src_rx_q][i_b]->pkt_len > 7000) { // do proper check on data length later
            // Handle them.
            std::size_t data_len = m_bufs[src_rx_q][i_b]->data_len;
            char* message = udp::get_udp_payload(m_bufs[src_rx_q][i_b]);
            handle_eth_payload(src_rx_q, message, data_len);
            m_num_frames[src_rx_q]++;
            m_num_bytes[src_rx_q] += data_len;

	    m_accum_ptr->process_packet(m_bufs[src_rx_q][i_b], dummy, dummy, dummy);
          }
        }

        // From John's example: more efficient bulk free
        rte_pktmbuf_free_bulk(m_bufs[src_rx_q], nb_rx);
        // Clear message buffers
       	//for (int i=0; i < nb_rx; i++) {
        //  rte_pktmbuf_free(m_bufs[src_rx_q][i]);
        //}

      } // per burst
    } // per Q
  } // main loop
 
  TLOG() << "LCore RX runner on CPU[" << lid << "] returned.";
  return 0;
}

} // namespace dpdklibs
} // namespace dunedaq
