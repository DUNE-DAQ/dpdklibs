# dpdklibs - DPDK UIO software and utilities 
Appfwk DAQModules, utilities, and scripts for I/O cards over DPDK.

# Setting up dpdk
For convenience, there is a set of scripts in this repo to set up dpdk, assuming
it has been installed and it works. These scripts have to be run as root. To
begin setting up dpdk, run

```
cd scripts
./kernel.sh
./hugepages.sh
```

The first script loads a new module to the kernel that will be used later to
bind the NICs. The second script sets up [hugepages](https://wiki.debian.org/Hugepages)

Now we have to identify the available NICs, to do that run `dpdk-devbind.py -s`

The output will look like
```
Network devices using kernel driver
============================================
0000:41:00.0 'Ethernet Connection X722 for 10GbE SFP+ 37d3' unused=i40e,uio_pci_generic
0000:41:00.1 'Ethernet Connection X722 for 10GbE SFP+ 37d3' unused=i40e,uio_pci_generic
0000:dc:00.0 'Ethernet Controller 10G X550T 1563' if=enp220s0f0 drv=ixgbe unused=uio_pci_generic 
0000:dc:00.1 'Ethernet Controller 10G X550T 1563' if=enp220s0f1 drv=ixgbe unused=uio_pci_generic *Active*
```
in this case the ones that we want to use for dpdk are the first two so we have to bind them. Modify `bind.sh`
to match the two addresses that we just found (in this case 0000:41:00.0 and 0000:41:00.1) and run
```
./bind.sh
```

Now if we run `dpdk-devbind.py -s` the output should look like this:
```
Network devices using DPDK-compatible driver
============================================
0000:41:00.0 'Ethernet Connection X722 for 10GbE SFP+ 37d3' drv=uio_pci_generic unused=i40e
0000:41:00.1 'Ethernet Connection X722 for 10GbE SFP+ 37d3' drv=uio_pci_generic unused=i40e

Network devices using kernel driver
===================================
0000:dc:00.0 'Ethernet Controller 10G X550T 1563' if=enp220s0f0 drv=ixgbe unused=uio_pci_generic 
0000:dc:00.1 'Ethernet Controller 10G X550T 1563' if=enp220s0f1 drv=ixgbe unused=uio_pci_generic *Active*
```
where the NICs that we wanted appear under `Network devices using DPDK-compatible driver`

# Troubleshooting

* EAL complains about `No available 1048576 kB hugepages reported`

* `ERROR: number of ports has to be even`
  This happens when the interfaces are not bound. See instructions above on
  binding the interfaces, modify `scripts/bind.sh` and run it

