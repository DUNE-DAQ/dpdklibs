# DAPHNE 256-sample receive build

The compact v4 DAPHNE Ethernet frame is 512 bytes. DPDK source processing
uses frame definitions compiled into its shared library; updating only the
format and readout libraries leaves a release DPDK binary with an incompatible
frame stride. Build dpdklibs locally alongside fddetdataformats and fdreadoutlibs
on marroyav/256_frame. The DPDK receive implementation itself is unchanged
from v2.3.8, acfd1845ac8f11dd09adc9f0c94c2c7b6de1f87e.

After adding the source tree to a DAQ workarea, source env.sh, regenerate
with cmake -S sourcecode -B build, then run dbt-build -j8. Verify the receiver
process loads the workarea install/dpdklibs/lib64/libdpdklibs.so. Match the
configured sender IP/MAC to captured packets: build 662c3fe on DAPHNE-015
sends from 192.168.0.100 / DE:AD:BE:EF:CA:FE.

Use a fresh receiver process after scrap: EAL reinitialization in an already
initialized process failed in this test and is not fixed by these changes.
