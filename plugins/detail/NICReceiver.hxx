namespace dunedaq {
namespace dpdklibs {

template<class T> 
int 
NICReceiver::rx_runner() {
  TLOG() << "Launching RX runner lcore.";
  bool once = true;
  uint16_t port;

  RTE_ETH_FOREACH_DEV(port)
  {
    if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
      TLOG() << "WARNING, port " << port << " is on remote NUMA node to polling thread! "
	     << "Performance will not be optimal.";
    }

    while (m_run_marker.load()) {
      /* Get burst of RX packets, from first port of pair. */
      // struct rte_mbuf* bufs[burst_size];
      const uint16_t nb_rx = rte_eth_rx_burst(port, 0, m_bufs, m_burst_size);
      if (nb_rx != 0) {

        if (once && m_bufs[0]->pkt_len > 8000) {
          const std::type_info& ti = typeid(T);
          TLOG() << "Serialize into type: " << ti.name();

          TLOG() << "nb_rx = " << nb_rx;
          TLOG() << "bufs.dta_len = " << m_bufs[0]->data_len;
      	  TLOG() << "bufs.pkt_len = " << m_bufs[0]->pkt_len;
          rte_pktmbuf_dump(stdout, m_bufs[0], m_bufs[0]->pkt_len);

          struct ipv4_udp_packet_hdr * udp_packet_hdr = rte_pktmbuf_mtod(m_bufs[0], struct ipv4_udp_packet_hdr *);

          char* pl = udp::get_udp_payload(m_bufs[0], 10);
 
          T target_payload;
	  TLOG() << "Size of target payload: " << sizeof(target_payload);
          

	  once = false;

	}

	// Doesn't correspond to the packets we are expecting to receive
        if (m_bufs[0]->data_len != 9000 + sizeof(struct rte_ether_hdr)) {
          //std::stringstream ss;
          //for (int i = 0; i < nb_rx; i++) {
          //  ss << m_bufs[i]->pkt_len << " ";
	    // TLOG() << "Found other data" << ss.str();
            // rte_pktmbuf_dump(stdout, bufs[i], bufs[i]->pkt_len);
          //}
          // continue;
        }



	for (int i=0; i<nb_rx; ++i) {
          m_num_frames++;
	}

	for (int i=0; i < nb_rx; i++)
        {
          rte_pktmbuf_free(m_bufs[i]);
        }

      }
    }
  } // RTE_ETH_FOREACH
 
  TLOG() << "Rx runner returned.";
}

} // namespace dpdklibs
} // namespace dunedaq
