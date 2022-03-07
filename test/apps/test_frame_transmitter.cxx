#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include "logging/Logging.hpp"
#include <chrono>
#include <thread>

#include "CyclicDataGenerator.hpp"

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define JUMBO_MBUF_BUF_SIZE 9200

#include "daqdataformats/WIBFrame.hpp"

const int packet_size = 150;
int burst_size = 32;
bool jumbo_enabled = true;
bool is_debug = true;


using namespace dunedaq::dpdklibs;

/* Default Ethernet configuration */
static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
};

/* Ethernet configuration for Jumbo frames */
static const struct rte_eth_conf port_conf_jumbo = {
    .rxmode = {
        .max_rx_pkt_len = 9000,
        .split_hdr_size = 0,
        .offloads = (DEV_RX_OFFLOAD_JUMBO_FRAME),
    },
    .txmode = {
        .mq_mode = ETH_MQ_TX_NONE,
        .offloads = (DEV_TX_OFFLOAD_IPV4_CKSUM |
                        DEV_TX_OFFLOAD_MULTI_SEGS),
    },
};

/* Print MAC address for a port */
void print_port_mac_addr(uint16_t port, struct rte_ether_addr *addr) {
    printf("INFO: Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                       " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
                    port,
                    addr->addr_bytes[0], addr->addr_bytes[1],
                    addr->addr_bytes[2], addr->addr_bytes[3],
                    addr->addr_bytes[4], addr->addr_bytes[5]);
}

/* Initializes a given port using global settings */
static inline int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf;

    if (jumbo_enabled) {
        port_conf = port_conf_jumbo;   // Use jumbo frame port configuration
    } else {
        port_conf = port_conf_default; // Use default port configuration
    }

    const uint16_t rx_rings = 1, tx_rings = 1;
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

    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    }

    /* Configure the ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0)
            return retval;
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    /* Allocate and set up 1 TX queue per Ethernet port */
    for (q = 0; q < tx_rings; q++) { 
        retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    }

    /* Start the Ethernet port. */
    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;
    
    /* Verify MAC address. */
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0)
        return retval;

    /* Display the port MAC address for information. */
    print_port_mac_addr(port, &addr);

    /* Enable RX in promiscuous mode for Ethernet device. */
    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0)
        return retval;

    return 0;
}

static __rte_noreturn void lcore_main(struct rte_mempool *mbuf_pool) {
    uint16_t port;

    /* Generator for cyclic dummy data. */
    cyclicdatagenerator::CyclicDataGenerator generator;

    /*
     * Check that the port is on the same NUMA node as the polling thread
     * for best performance.
     */
    RTE_ETH_FOREACH_DEV(port) {
        if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
            printf("WARNING, port %u is on remote NUMA node to "
                            "polling thread./n/tPerformance will "
                            "not be optimal.\n", port);
        }

        printf("INFO: Port %u has socket id: %u.\n", port, rte_eth_dev_socket_id(port));
    }

    printf("\n\nCore %u transmitting packets. [Ctrl+C to quit]\n\n", rte_lcore_id());

    /* Run until the application is quit or killed. */
    for (;;) {
        /* 
         * Transmit packets on port.
         */
        RTE_ETH_FOREACH_DEV(port) {

            TLOG() << "Sending message";
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));

            // Message struct
            struct Message {
                char data[packet_size];
            };

            // Ethernet header 
            struct rte_ether_hdr eth_hdr = {0};
        eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

            /* Dummy message to transmit */
            struct Message msg = {};
            generator.get_prev_n(msg.data, packet_size);

            
            //struct rte_mbuf *pkt[burst_size];
            struct rte_mbuf **pkt = (rte_mbuf**) malloc(sizeof(struct rte_mbuf*) * burst_size);
            rte_pktmbuf_alloc_bulk(mbuf_pool, pkt, burst_size);

            int i;
            for (i = 0; i < burst_size; i++) {
                pkt[i]->data_len = pkt[i]->pkt_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr)
                            + sizeof(struct rte_udp_hdr) + sizeof(struct Message);

        char *ether_mbuf_offset = rte_pktmbuf_mtod_offset(pkt[i], char*, 0);
        char *msg_mbuf_offset = rte_pktmbuf_mtod_offset(pkt[i], char*, sizeof(struct rte_ether_hdr) + sizeof(struct Message));
        
        rte_memcpy(ether_mbuf_offset, &eth_hdr, sizeof(rte_ether_hdr));
        rte_memcpy(msg_mbuf_offset, &msg, sizeof(struct Message));

        if (is_debug) {
            rte_pktmbuf_dump(stdout, pkt[i], pkt[i]->pkt_len);
        }
            }

            /* Send burst of TX packets. */
            uint16_t nb_tx = rte_eth_tx_burst(port, 0, pkt, burst_size);    

            /* Free any unsent packets. */
            if (unlikely(nb_tx < burst_size)) {
                uint16_t buf;
                for (buf = nb_tx; buf < burst_size; buf++) {
                    rte_pktmbuf_free(pkt[buf]);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;

    /* Init EAL */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "ERROR: EAL initialization failed.\n");
    }

    argc -= ret;
    argv += ret;

    /* Check that there is an even number of ports to send/receive on. */
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports < 2 || (nb_ports & 1)) {
        rte_exit(EXIT_FAILURE, "ERROR: number of ports must be even\n");
    }

    printf("RTE_MBUF_DEFAULT_BUF_SIZE = %d\n", RTE_MBUF_DEFAULT_BUF_SIZE);

    if (jumbo_enabled) {
        mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
            MBUF_CACHE_SIZE, 0, JUMBO_MBUF_BUF_SIZE, rte_socket_id());
    } else {
        mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
            MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    }

    if (mbuf_pool == NULL) {
        rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
    }

    /* Initialize all ports. */
    RTE_ETH_FOREACH_DEV(portid) { 
        if (port_init(portid, mbuf_pool) != 0) {
            rte_exit(EXIT_FAILURE, "ERROR: Cannot init port %"PRIu16 "\n", portid);
        }
    }

    if (rte_lcore_count() > 1) { 
        printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
    }

    /* Call lcore_main on the main core only. */
    lcore_main(mbuf_pool);

    /* clean up the EAL */
    rte_eal_cleanup();

    return 0;
}
