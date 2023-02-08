# Intel NIC Setup

**Work in progress**

* Set MTUs to Jumbi frames
* Configure hugepages using dpdk utility script
    - Do we need `dpdk-hugepages.py`?


```
    sudo cp 52-hugepages.rules /etc/udev/rules.d/
    sudo cp 53-vfio.rules /etc/udev/rules.d/
```