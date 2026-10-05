#ifndef DPDKLIBS_INCLUDE_DPDKLIBS_XSTATSHELPER_HPP_
#define DPDKLIBS_INCLUDE_DPDKLIBS_XSTATSHELPER_HPP_

#include <rte_ethdev.h>

namespace dunedaq::dpdklibs {

  struct IfaceXstats {
    IfaceXstats(){}
    ~IfaceXstats() 
    {
      clear();
    }

    void clear();
    void setup(int iface);
    void reset_counters();
    void poll();
    void stop();

    int m_iface_id;
    bool m_enabled = false;
    struct rte_eth_stats m_eth_stats;
    struct rte_eth_xstat_name *m_xstats_names = nullptr;
    uint64_t *m_xstats_ids = nullptr;
    uint64_t *m_xstats_values = nullptr;
    int m_len;

  };

}

#endif // DPDKLIBS_INCLUDE_DPDKLIBS_XSTATSHELPER_HPP_
