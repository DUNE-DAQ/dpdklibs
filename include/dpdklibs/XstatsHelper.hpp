#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_XSTATSHELPER_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_XSTATSHELPER_HPP_

#include <rte_ethdev.h>

namespace dunedaq::dpdklibs {

  struct IfaceXstats {
    IfaceXstats(){}
    ~IfaceXstats() 
    {
      if (m_xstats_values != nullptr) {
        free(m_xstats_values);
      }
      if (m_xstats_ids != nullptr) {
        free(m_xstats_ids);
      }
      if (m_xstats_names != nullptr) {
        free(m_xstats_names);
      }
      if (m_xstats_current != nullptr) free(m_xstats_current);   // modified by RMA
      if (m_xstats_previous != nullptr) free(m_xstats_previous); // modified by RMA

    }

    void setup(int iface) {
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
      m_xstats_values   = (uint64_t*)(malloc(sizeof(uint64_t) * m_len));
      m_xstats_current  = (uint64_t*)(malloc(sizeof(uint64_t) * m_len)); // modified by RMA
      m_xstats_previous = (uint64_t*)(malloc(sizeof(uint64_t) * m_len)); // modified by RMA
      if (m_xstats_values == nullptr || m_xstats_current == nullptr || m_xstats_previous == nullptr) {
        printf("Cannot allocate memory for xstats\n");
      }

      // Read initial HW values into current; previous starts at zero (HW was just reset above) // modified by RMA
      if (m_len != rte_eth_xstats_get_by_id(m_iface_id, nullptr, m_xstats_current, m_len)) { // modified by RMA
        printf("Cannot get xstat values\n");
      }
      memset(m_xstats_previous, 0, sizeof(uint64_t) * m_len);         // modified by RMA
      memset(&m_eth_stats_previous, 0, sizeof(m_eth_stats_previous));  // modified by RMA
      memset(&m_eth_stats_current, 0, sizeof(m_eth_stats_current));  // modified by RMA


      // Print all xstats names and values to be amazed (WOW!)
      TLOG() << "Registered HW based metrics: ";        
      for (int i = 0; i < m_len; i++) {
        TLOG() << "  XName: " << m_xstats_names[i].name;
      }

      m_allocated = true;
    };

    // void reset_counters() {
    //   if (m_allocated) {
    //     rte_eth_xstats_reset(m_iface_id); //{
    //     //  TLOG() << "Cannot reset xstat values!";
    //     //} else { 
    //     //}
    //   }
    // }

    // modified by RMA: software reset instead of HW reset (rte_eth_xstats_reset not supported on Mellanox)
    void reset_counters() {
      if (m_allocated) {
        memcpy(m_xstats_previous, m_xstats_current, sizeof(uint64_t) * m_len); // modified by RMA
        memcpy(&m_eth_stats_previous, &m_eth_stats_current, sizeof(m_eth_stats_previous)); // modified by RMA
      }
    }


    // void poll() {
    //   if (m_allocated) {
    //     if (m_len != rte_eth_xstats_get_by_id(m_iface_id, nullptr, m_xstats_values, m_len)) {
    //       TLOG() << "Cannot get xstat values!";
    //     //} else { 
    //     }

    //     rte_eth_stats_get(m_iface_id, &m_eth_stats);
    //   }
    // }

    void poll() {
      if (m_allocated) {
        if (m_len != rte_eth_xstats_get_by_id(m_iface_id, nullptr, m_xstats_current, m_len)) { // modified by RMA
          TLOG() << "Cannot get xstat values!";
        }
        rte_eth_stats_get(m_iface_id, &m_eth_stats_current); // modified by RMA

        for (int i = 0; i < m_len; ++i)                                                         // modified by RMA
          m_xstats_values[i] = m_xstats_current[i] - m_xstats_previous[i];                     // modified by RMA

        m_eth_stats.ipackets  = m_eth_stats_current.ipackets  - m_eth_stats_previous.ipackets;  // modified by RMA
        m_eth_stats.opackets  = m_eth_stats_current.opackets  - m_eth_stats_previous.opackets;  // modified by RMA
        m_eth_stats.ibytes    = m_eth_stats_current.ibytes    - m_eth_stats_previous.ibytes;    // modified by RMA
        m_eth_stats.obytes    = m_eth_stats_current.obytes    - m_eth_stats_previous.obytes;    // modified by RMA
        m_eth_stats.imissed   = m_eth_stats_current.imissed   - m_eth_stats_previous.imissed;   // modified by RMA
        m_eth_stats.ierrors   = m_eth_stats_current.ierrors   - m_eth_stats_previous.ierrors;   // modified by RMA
        m_eth_stats.oerrors   = m_eth_stats_current.oerrors   - m_eth_stats_previous.oerrors;   // modified by RMA
        m_eth_stats.rx_nombuf = m_eth_stats_current.rx_nombuf - m_eth_stats_previous.rx_nombuf; // modified by RMA
      }
    }


    int m_iface_id;
    bool m_allocated = false;
    struct rte_eth_stats m_eth_stats;
    struct rte_eth_stats m_eth_stats_current;   // modified by RMA
    struct rte_eth_stats m_eth_stats_previous;  // modified by RMA
    struct rte_eth_xstat_name *m_xstats_names;
    uint64_t *m_xstats_ids;
    uint64_t *m_xstats_values;
    int m_len;
    uint64_t *m_xstats_current  = nullptr;  // modified by RMA
    uint64_t *m_xstats_previous = nullptr;  // modified by RMA



  };

}

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_XSTATSHELPER_HPP_
