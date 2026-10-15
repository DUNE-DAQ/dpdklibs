#include "IfaceWrapper.hpp"

namespace dunedaq::dpdklibs {

  void IfaceXstats::clear() {
    if (m_xstats_values != nullptr) {
      free(m_xstats_values);
      m_xstats_values = nullptr;
    }
    if (m_xstats_ids != nullptr) {
      free(m_xstats_ids);
      m_xstats_ids = nullptr;
    }
    if (m_xstats_names != nullptr) {
      free(m_xstats_names);
      m_xstats_names = nullptr;
    }
    m_len = 0;
  }

  void IfaceXstats::setup(int iface) {
    m_iface_id = iface;
    rte_eth_stats_reset(m_iface_id);
    rte_eth_xstats_reset(m_iface_id);

    // Get number of stats
    m_len = rte_eth_xstats_get_names_by_id(m_iface_id, NULL, 0, NULL);
    if (m_len < 0) {
      printf("Cannot get xstats count\n");
    }

    // Get names of HW registered stat fields
    m_xstats_names = (rte_eth_xstat_name*)(malloc(sizeof(struct rte_eth_xstat_name) * m_len));
    if (m_xstats_names == nullptr) {
      printf("Cannot allocate memory for xstat names\n");
    }

    // Retrieve xstats names, passing NULL for IDs to return all statistics
    if (m_len != rte_eth_xstats_get_names(m_iface_id, m_xstats_names, m_len)) {
      printf("Cannot get xstat names\n");
    }

    // Allocate value fields
    m_xstats_values = (uint64_t*)(malloc(sizeof(m_xstats_values) * m_len));
    if (m_xstats_values == nullptr) {
      printf("Cannot allocate memory for xstats\n");
    }

    // Getting xstats values (this is that we call in a loop/get_info
    if (m_len != rte_eth_xstats_get_by_id(m_iface_id, nullptr, m_xstats_values, m_len)) {
      printf("Cannot get xstat values\n");
    }

    // Print all xstats names and values to be amazed (WOW!)
    TLOG() << "Registered HW based metrics: ";        
    for (int i = 0; i < m_len; i++) {
      TLOG() << "  XName: " << m_xstats_names[i].name;
    }

    m_enabled = true;
  };

  void IfaceXstats::reset_counters() {
    if (m_enabled) {
      rte_eth_xstats_reset(m_iface_id); //{
      //  TLOG() << "Cannot reset xstat values!";
      //} else { 
      //}
    }
  }

  void IfaceXstats::poll() {
    if (m_enabled) {
      if (m_len != rte_eth_xstats_get_by_id(m_iface_id, nullptr, m_xstats_values, m_len)) {
        TLOG() << "Cannot get xstat values!";
      //} else { 
      }

      rte_eth_stats_get(m_iface_id, &m_eth_stats);
    }
  }

  void IfaceXstats::stop() {
    m_enabled = false;
    clear();
    TLOG() << "XstatsHelper disabled.";
  }
} // namespace dunedaq::dpdklibs