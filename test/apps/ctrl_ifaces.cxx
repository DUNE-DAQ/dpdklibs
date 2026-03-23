#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include "CLI/App.hpp"
#include "CLI/Config.hpp"
#include "CLI/Formatter.hpp"

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>

void ifaces_down() {
  uint16_t portid
  int ret;

  RTE_ETH_FOREACH_DEV(portid) {
    printf("bringing down port %d...", portid);
    ret = rte_eth_dev_stop(portid);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "rte_eth_dev_stop: err=%s, port=%u\n",
              strerror(-ret), portid);
    rte_eth_dev_set_link_down(portid);
    printf(" Done\n");
  }
  return 0  
}

void ifaces_up() {
  uint16_t portid
  int ret;

  RTE_ETH_FOREACH_DEV(portid) {
    printf("bringing up port %d...", portid);
    rte_eth_dev_set_link_up(portid);
    ret = rte_eth_dev_start(portid);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%s, port=%u\n",
              strerror(-ret), portid);
    printf(" Done\n");
  }
  return 0  
}

int main() {

  bool up;
  bool down;

  CLI::App app{"test frame receiver"};
  app.add_flag("-u,--up", up, "bring up all interfaces.");
  app.add_flag("-d,--down", down, "bring down all interfaces.");

  if (down & up) {
    printf("Can only specify one of the flags. Exiting...")
    exit(1)
  }
  if(down) {
    printf("bringing down all interfaces on server.");
    ifaces_down();
  }
  if(up) {
    printf("bringing up all interfaces on server.");
    ifaces_up();
  }


}
