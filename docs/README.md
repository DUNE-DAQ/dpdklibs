# dpdklibs - Data Plane Development Kit userspace I/O software and utilities 

## Overview

The dpdklibs package makes use of the [Data Plane Development
Kit](https://www.dpdk.org/)(DPDK) to allow for faster dataflow in the
DUNE DAQ. DPDK increases packet processing speed on multi-core
CPUs by allowing a userspace software application to communicate
directly with a Network Interface Card (NIC) _without_ needing to go through the OS
kernel. dpdklibs consists of:

1. Utilities and scripts for testing UDP packet flow (`dpdklibs_test_frame_receiver`, etc.)
1. DAQ modules for communication (`NICSender` and `NICReceiver`)
1. A general-purpose library containing functions others can use for communication (`get_udp_payload`, etc.)

## Preparing A System

More than most DUNE DAQ packages, this one gets "close to the metal";
as such, some preparation of the nodes is needed for this package to
work on them. The steps described/linked to in this section should
only be performed by experts with root access to the system they're
on; users who are on a system which has already been prepared can skip
to the next section. Note that some of the steps need to be done not
just once, but every time a server reboots.  

1. Ensure that the NIC is configured for DPDK based applications on np04:
https://github.com/DUNE-DAQ/dpdklibs/wiki/DPDK-based-NIC-configuration-on-servers

1. Install both DPDK and [OpenFabrics Enterprise
Distribution](https://www.openfabrics.org/ofed-for-linux/)'s kernel
bypass software; instructions on the dpdklibs Wiki
[here](https://github.com/DUNE-DAQ/dpdklibs/wiki/OFED-&-DPDK-installation)

1. Ensure that an Input-Output Memory Management Unit (IOMMU),
connecting a DMA-capable IO bus to the main memory, is on in the BIOS
when your server boots. Instructions [here](https://github.com/DUNE-DAQ/dpdklibs/wiki/IOMMU-configuration). 


## How to run a system with a transmitter and/or a receiver:
Generate the config with
```
python sourcecode/dpdklibs/scripts/dpdklibs_gen.py -c conf.json dpdk_app
```

where `conf.json` has the parameters that we want to use (see
`schema/dpdklibs/confgen.jsonnet` for a complete list of all the parameters) and
then run it (as root)

```
nanorc dpdk_app partition_name
```

Only the sender and only the receiver can be started with the parameters
`only_sender` and `only_receiver` respectively. This is an example for the
`conf.json` that enables only the sender:

```
{
    "dpdklibs": {
        "only_sender": true
    }
}
```

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


# Tests and examples
There are a set of tests or example applications

* `dpdklibs_test_basic_frame_receiver` and
  `dpdklibs_test_basic_frame_transmitter` do transmission of `WIBFrames` and
  check that there are no missing frames. Each is run in a different node and
  the receiver will tell us after every burst of packets if all frames have been
  found.
