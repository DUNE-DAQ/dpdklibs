#!/bin/bash

echo "Set MTU for NIC ports..."
ifconfig enp129s0f0 mtu 9600
ifconfig enp129s0f1 mtu 9600

echo "Add Hugepages on every NUMA node..."
dpdk-hugepages.py -c
echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node2/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node3/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node4/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node5/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node6/hugepages/hugepages-2048kB/nr_hugepages
echo 1024 > /sys/devices/system/node/node7/hugepages/hugepages-2048kB/nr_hugepages
dpdk-hugepages.py -m

echo "Modprobe drivers..."
modprobe uio_pci_generic
modprobe vfio_pci

echo "Bind NIC port with VFIO-PCI..."
dpdk-devbind.py -u 0000:81:00.0
dpdk-devbind.py -b vfio-pci 0000:81:00.0

echo "Nuke permissions for HugeP and VFIO devs..."
chmod -R 777 /dev/hugepages
chmod -R 777 /dev/vfio

echo "Copy expected limits.conf..."
cp /opt/limits.conf /etc/security/limits.conf

