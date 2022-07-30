
namespace dunedaq {
namespace dpdklibs {

//template<class T> 
int 
NICReceiver::rx_runner(void *arg __rte_unused) {
  TLOG() << "Launching RX runner lcore.";
  bool once = true;
  uint16_t port = 0;

  const uint16_t lid = rte_lcore_id();

  auto queues = m_rx_core_map[lid];

  if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
    TLOG() << "WARNING, port " << port << " is on remote NUMA node to polling thread! "
           << "Performance will not be optimal.";
  }

  //while(!m_run_marker) {
  while(!ealutils::dpdk_quit_signal){

    for (auto q : queues) {
      auto queue = q.first;
      // Get burst from queue
      const uint16_t nb_rx = rte_eth_rx_burst(port, queue, m_bufs[queue], m_burst_size);
      if (nb_rx != 0) {

        if (once && m_bufs[queue][0]->pkt_len > 8000) { // Print first packet for FYI
          //const std::type_info& ti = typeid(T);
          //TLOG() << "Serialize into type: " << ti.name();
          //char[] target_payload;
	    //TLOG() << "Size of target payload: " << sizeof(target_payload);
          TLOG() << "lid = " << lid;
          TLOG() << "queue = " << queue;
          TLOG() << "nb_rx = " << nb_rx;
          TLOG() << "bufs.dta_len = " << m_bufs[queue][0]->data_len;
      	  TLOG() << "bufs.pkt_len = " << m_bufs[queue][0]->pkt_len;
          rte_pktmbuf_dump(stdout, m_bufs[queue][0], m_bufs[queue][0]->pkt_len);
 	    once = false;
	  }

	  // Iterate on burst packets
	  for (int i=0; i<nb_rx; ++i) {

	    //// Avoid ARP
	    //if (udp::foff_arp(m_bufs[queue][i])) {
	    //  //udp::dump_udp_header(m_bufs[queue][i]);
	    //  rte_pktmbuf_free(m_bufs[queue][i]);
   	    //  continue;
	    //} 

	    //// Avoid non IPV4 packets
	    //if (not RTE_ETH_IS_IPV4_HDR(m_bufs[queue][i]->packet_type)) {
	    //  //udp::dump_udp_header(m_bufs[queue][i]);
 	    //  rte_pktmbuf_free(m_bufs[queue][i]);
   	    //  continue;
	    //}
	  
	    // Check for JUMBOs (user payloads)
	    if (true) { // do proper check on data length later
            m_num_frames[queue]++;
            std::size_t data_len = m_bufs[queue][i]->data_len;
            char* message = udp::get_udp_payload(m_bufs[queue][i]);  //(char *)(udp_packet + 1);
            // Copy data from network stack
            copy_out(queue, message, data_len);
	    }

          // Clear packet
          rte_pktmbuf_free(m_bufs[queue][i]);

	  }

        // Clear message buffers
	  //for (int i=0; i < nb_rx; i++) {
        //  rte_pktmbuf_free(m_bufs[queue][i]);
        //}

      } // per burst
    } // per Q
  }
 
  TLOG() << "Rx runner returned.";
  return 0;
}

} // namespace dpdklibs
} // namespace dunedaq
