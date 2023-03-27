/* Application will run until quit or killed. */

#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <sstream>
#include <stdint.h>
#include <limits>
#include <iomanip>
#include <fstream>
#include <csignal>

#include "logging/Logging.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"
#include "dpdklibs/udp/Utils.hpp"

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = 9000,
        .offloads = (DEV_RX_OFFLOAD_IPV4_CKSUM | DEV_RX_OFFLOAD_UDP_CKSUM),
        }
};

static int
lcore_main(struct rte_mempool *mbuf_pool)
{
  uint16_t iface = std::numeric_limits<uint16_t>::max();
  TLOG() << "Launch lcore for interface: " << iface;
  return 0;
}

int
main(int argc, char* argv[])
{  
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
  }
  
  auto nb_ifaces = rte_eth_dev_count_avail();
  TLOG() << "# of available interfaces: " << nb_ifaces;
  
  //struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
	//						    MBUF_CACHE_SIZE, 0, default_mbuf_size, rte_socket_id());

  //if (mbuf_pool == NULL) {
  //  rte_exit(EXIT_FAILURE, "ERROR: rte_pktmbuf_pool_create returned null");
  //}

  //lcore_main(mbuf_pool);
  rte_eal_cleanup();
  
  return 0;
}
