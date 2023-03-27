#include <inttypes.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

#include "logging/Logging.hpp"
#include "detdataformats/wib/WIBFrame.hpp"
#include "dpdklibs/udp/PacketCtor.hpp"

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define PG_JUMBO_FRAME_LEN (9600 + RTE_ETHER_CRC_LEN + RTE_ETHER_HDR_LEN)
#ifndef RTE_JUMBO_ETHER_MTU
#define RTE_JUMBO_ETHER_MTU (PG_JUMBO_FRAME_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN) /*< Ethernet MTU. */
#endif


// Apparently only 8 and above works
int burst_size = 1;
bool is_debug = false;

using namespace dunedaq;
using namespace dpdklibs;
using namespace udp;

static const struct rte_eth_conf port_conf_default = {
    // .txmode = {
    //   .mtu = 9000,
    // },
    .txmode = {
        .offloads = (DEV_TX_OFFLOAD_IPV4_CKSUM |
                     DEV_TX_OFFLOAD_UDP_CKSUM),
},
};

static inline int
port_init(uint16_t port)
{
  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = 0, tx_rings = 2;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf txconf;

  if (!rte_eth_dev_is_valid_port(port))
    return -1;

  retval = rte_eth_dev_info_get(port, &dev_info);
  if (retval != 0) {
    printf("Error during getting device (port %u) info: %s\n", port, strerror(-retval));
    return retval;
  }

  if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
    port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0)
    return retval;

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0)
    return retval;

  txconf = dev_info.default_txconf;
  txconf.offloads = port_conf.txmode.offloads;
  /* Allocate and set up 1 TX queue per Ethernet port. */
  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0)
      return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if (retval < 0)
    return retval;

  /* Display the port MAC address. */
  struct rte_ether_addr addr;
  retval = rte_eth_macaddr_get(port, &addr);
  if (retval != 0)
    return retval;

  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
         port,
         addr.addr_bytes[0],
         addr.addr_bytes[1],
         addr.addr_bytes[2],
         addr.addr_bytes[3],
         addr.addr_bytes[4],
         addr.addr_bytes[5]);

  rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
  
  uint16_t mtu;
  rte_eth_dev_get_mtu(port, &mtu);
  TLOG() << "MTU = " << mtu;

  /* Enable RX in promiscuous mode for the Ethernet device. */
  // retval = rte_eth_promiscuous_enable(port);
  // if (retval != 0)
  //   return retval;

  return 0;
}

void lcore_main(void *arg) {


    uint16_t lid = rte_lcore_id();

    // if (pktgen_has_work())
    //     return 0;
    TLOG() << "lid = " << lid;
    if (lid > 2) return;

    TLOG () << "Going to sleep with lid = " << lid;
    rte_delay_us_sleep((lid + 1) * 1000021);
    uint16_t port;

    unsigned nb_ports = rte_eth_dev_count_avail();
    TLOG () << "mbuf with lid = " << lid;
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create((std::string("MBUF_POOL") + std::to_string(lid)).c_str(), NUM_MBUFS * nb_ports,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_pool_create failed, info: %s\n", rte_strerror(rte_errno));
    }
    TLOG () << "mbuf done with lid = " << lid;

    uint16_t portid;
    // Initialize all ports
    // RTE_ETH_FOREACH_DEV(portid) {
    // if (port_init(portid) != 0) {
    //     rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    // }
    // }

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  // RTE_ETH_FOREACH_DEV(port) {
  //     if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
  //         printf("WARNING, port %u is on remote NUMA node to "
  //                         "polling thread./n/tPerformance will "
  //                         "not be optimal.\n", port);
  //     }

  //     printf("INFO: Port %u has socket id: %u.\n", port, rte_eth_dev_socket_id(port));
  // }

    TLOG() << "\n\nCore " << rte_lcore_id() << " transmitting packets. [Ctrl+C to quit]\n\n";

  /* Run until the application is quit or killed. */
  int burst_number = 0;
  std::atomic<int> num_frames = 0;

  auto stats = std::thread([&]() {
    while (true) {
      // TLOG() << "Rate is " << (sizeof(detdataformats::wib::WIBFrame) + sizeof(struct rte_ether_hdr)) * num_frames / 1e6 * 8;
      TLOG() << "[DO NOT TRUST] Rate is " << sizeof(struct ipv4_udp_packet) * num_frames / 1e6 * 8;
      num_frames.exchange(0);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  // std::this_thread::sleep_for(std::chrono::milliseconds(5));
  struct rte_mbuf **pkt = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
  if (pkt == NULL) {
    TLOG(TLVL_ERROR) << "Call to malloc failed; exiting...";
    std::exit(1);
  }
  
  int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, pkt, burst_size);

  if (retval != 0) {
    rte_exit(EXIT_FAILURE, "ERROR: call to rte_pktmbuf_alloc_bulk failed, info: %s\n", strerror(abs(retval)));
  }

  std::string first_string = std::string(8000, '9');
  std::string second_string = std::string(8000, '7');


  for (;;) {
    /*
     * Transmit packets on port.
     */
      port = 0;

      // std::this_thread::sleep_for(std::chrono::microseconds(5));

      // Message struct
      // struct Message {
      //   // detdataformats::wib::WIBFrame fr;
      //   char ch[16];
      // };

      // Ethernet header
      // struct rte_ether_hdr eth_hdr = {0};
      // eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

      /* Dummy message to transmit */


      //struct rte_mbuf *pkt[burst_size];

      // if (burst_number % 1000 == 0) {
      //   TLOG() << "burst_number =" << burst_number;
      // }

      for (int i = 0; i < burst_size; i++)
      {
        // struct Message msg;

        struct ipv4_udp_packet msg;
        pktgen_packet_ctor(&msg.hdr);

        // msg.fr.set_timestamp(burst_number);
        // msg.fr.set_channel(190, i);
        pkt[i]->pkt_len = sizeof(struct ipv4_udp_packet);
        pkt[i]->data_len = 8000;
        // for (int i = 0; i < 8000 / 11; ++i) {
        //     strcpy(msg.payload + i * 11, "Hello world");
        // }
        // if (port) 
        //     strcpy(msg.payload, first_string.c_str());
        // else
        //     strcpy(msg.payload, second_string.c_str());

        char *ether_mbuf_offset = rte_pktmbuf_mtod_offset(pkt[i], char*, 0);
        // char *msg_mbuf_offset = rte_pktmbuf_mtod_offset(pkt[i], char*, sizeof(struct rte_ether_hdr));

        // rte_memcpy(ether_mbuf_offset, &eth_hdr, sizeof(rte_ether_hdr));
        // rte_memcpy(msg_mbuf_offset, &msg, sizeof(struct Message));

        rte_memcpy(ether_mbuf_offset, &msg, sizeof(struct ipv4_udp_packet));


        if (false) {
          rte_pktmbuf_dump(stdout, pkt[i], pkt[i]->pkt_len);
        }
      }
      burst_number++;

      /* Send burst of TX packets. */
      int sent = 0;
      uint16_t nb_tx;
      while(sent < burst_size)
      {
	nb_tx = rte_eth_tx_burst(port, lid-1, pkt, burst_size - sent);
        sent += nb_tx;
        num_frames += nb_tx;
      }

      /* Free any unsent packets. */
      if (unlikely(nb_tx < burst_size))
      {
        uint16_t buf;
        for (buf = nb_tx; buf < burst_size; buf++)
        {
          rte_pktmbuf_free(pkt[buf]);
        }
      }
      retval = rte_eth_tx_done_cleanup(port, lid-1, 0);
      if (retval != 0) {
	rte_exit(EXIT_FAILURE, "ERROR: failure calling rte_eth_tx_done_cleanup, info: %s\n", strerror(abs(retval)));
      }
  }
}

int main(int argc, char* argv[]) {
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    // Do *not* try to use argv after call to rte_eal_init; this function can modify the array
    int retval = rte_eal_init(argc, argv);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed, info: %s\n", strerror(abs(retval)));
    }

    // Check that there is an even number of ports to send/receive on
    nb_ports = rte_eth_dev_count_avail();
    TLOG() << "There are " << nb_ports << " ports available out of a total of " << rte_eth_dev_count_total();
    if (nb_ports == 0) {
      rte_exit(EXIT_FAILURE, "ERROR: 0 ports are available. This can be caused either by someone else currently\nusing dpdk-based code or the necessary drivers not being bound to the NICs\n(see https://github.com/DUNE-DAQ/dpdklibs#readme for more)\n");
    }
    if (nb_ports < 2 || (nb_ports & 1)) {
        rte_exit(EXIT_FAILURE, "ERROR: number of available ports must be even and greater than 0\n");
    }

    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 0, tx_rings = 2;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;
    uint16_t port = 0;

    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_configure failed, info: %s\n", strerror(abs(retval)));
    }

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_adjust_nb_rx_tx_desc failed, info: %s\n", strerror(abs(retval)));
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
      retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
      if (retval < 0) {
	rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_tx_queue_setup failed, info: %s\n", strerror(abs(retval)));
      }
    }

    retval = rte_eth_dev_start(port);
    if (retval < 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_start failed, info: %s\n", strerror(abs(retval)));
    }

    /* Display the port MAC address. */
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_macaddr_get failed, info: %s\n", strerror(abs(retval)));
    }

    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
           port,
           addr.addr_bytes[0],
           addr.addr_bytes[1],
           addr.addr_bytes[2],
           addr.addr_bytes[3],
           addr.addr_bytes[4],
           addr.addr_bytes[5]);

    retval = rte_eth_dev_set_mtu(port, RTE_JUMBO_ETHER_MTU);
    if (retval != 0) {
      rte_exit(EXIT_FAILURE, "ERROR: call to rte_eth_dev_set_mtu failed, info: %s\n", strerror(abs(retval)));
    }
    
    uint16_t mtu;
    rte_eth_dev_get_mtu(port, &mtu);
    TLOG() << "MTU = " << mtu;

    // mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
    //     MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    // if (mbuf_pool == NULL) {
    //     rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    // }

    // Initialize all ports
    // RTE_ETH_FOREACH_DEV(portid) {
    //     if (port_init(portid, mbuf_pool) != 0) {
    //         rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    //     }
    // }

    // Call lcore_main on the main core only
    // int res = rte_eal_remote_launch((lcore_function_t *) lcore_main, mbuf_pool, 1);

    // int res2 = rte_eal_remote_launch((lcore_function_t *) lcore_main, mbuf_pool, 2);
    rte_eal_mp_remote_launch((lcore_function_t *) lcore_main, NULL, SKIP_MAIN);
    // TLOG() << "Result = " << res;
    // TLOG() << "Result = " << res;
    // lcore_main(mbuf_pool);

    // rte_eal_wait_lcore(1);
    // rte_eal_wait_lcore(2);
    // clean up the EAL
    // rte_eal_cleanup();
    rte_eal_mp_wait_lcore();

    return 0;
}
