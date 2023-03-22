#!/bin/bash

ifconfig ens801f0np0 mtu 9600

/opt/dpdk/dpdk-22.03/usertools/dpdk-hugepages.py -c

echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

/opt/dpdk/dpdk-22.03/usertools/dpdk-hugepages.py -m


