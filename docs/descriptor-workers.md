# Descriptor workers

Set `PDSRawDataProcessor.separate_descriptor_processing` to true and assign
`DPDKPortConfiguration.descriptor_cores` to dedicated CPUs on the NIC's NUMA
node. These CPUs must be distinct from DPDK lcores and raw processing CPUs.
The TP output must use `kFollyMPMCQueue`; configuration rejects other outputs.
`descriptor_queue_batches` defaults to 256, a power of two >= 2.

RX copies the 64-byte DAPHNE metadata into a bounded descriptor queue before
offering the full waveform to the raw queue. A full raw queue therefore does
not discard descriptor work. NIC loss still affects both paths, and each
queue can independently overflow. This changes host processing only; packets
on the wire still contain waveforms.

Source and hardware channel select the descriptor worker. Every RX queue
has a separate SPSC lane per worker. A source must belong to one RX queue,
preserving channel order; workers do not establish global timestamp order.
Live TA processing still needs a merge policy.

The raw callback retains timestamp checks and buffering. TP generation runs
only on descriptor workers when enabled; the default retains inline TPG.
Descriptor conversion uses the existing channel map, channel mask and integral
threshold. Overflowing descriptors are rejected and counted separately from
malformed frames and failed TP sends.

`FrameWorkerInfo` adds a `path` label (`raw` or `descriptor`) alongside the
processing core. Its enqueue, process, drop and callback-failure counters
measure each queue separately. `SourceInfo` reports cumulative descriptor
frames processed, overflows and malformed frames since run start.
`HitFindingInfo` retains interval TP sent/send-failure counters.
Stopping flow quiesces RX and drains both paths.

`DescriptorFork_test` blocks raw processing until its queue drops frames,
while two descriptor workers complete 1000 TPs and preserve channel order.
`PDSDescriptorProcessor_test` covers conversion, filtering, rejection counters,
failed sends and shared processor ownership.

Local portable test drivers passed address, undefined-behavior and thread
sanitizers. The CERN release build passed, along with all four dpdklibs and
seven fdreadoutlibs tests.

## CERN measurements

Runs 45343 and 45344 used gateware `14f56c3`, continuation off, 16 connected
channels, and independent 10 ms DAQ windows at 10 Hz. Raw CPUs were 1 and 5;
descriptor CPUs were 9 and 13, adding 2 and 6 for the four-worker comparison.
Both queues used 256 batches. Ten-second phases scanned 50, 80 and 120 kHz
at descriptor threshold 8, followed by 120 kHz at quiet threshold 64.
Actual software trigger rates were approximately 49.9, 79.7 and 119.2 kHz.

Steady monitoring samples, excluding phase transitions, showed:

| Descriptor workers | Target kHz | Threshold | NIC loss | Raw queue loss | Descriptor queue loss |
| --- | --- | --- | --- | --- | --- |
| 2 | 50 | 8 | 0% | 0% | 0% |
| 2 | 80 | 8 | 0% | 0.21% | 0% |
| 2 | 120 | 8 | 0% | 4.37% | 0% |
| 2 | 120 | 64 | 0% | 5.13% | 0% |
| 4 | 50 | 8 | 0% | 0% | 0% |
| 4 | 80 | 8 | 0% | 5.24% | 0% |
| 4 | 120 | 8 | 0% | 15.67% | 0% |
| 4 | 120 | 64 | 0% | 13.44% | 0% |

Across complete profiles, including transitions, two descriptor workers
processed all 73,733,216 offered frames with zero drops. Four workers dropped
2,625 of 73,614,864 offered frames (0.0036%), outside the sampled steady
intervals. NIC missed/error/no-buffer counters were zero across both profiles.
All enqueued frames drained; descriptor malformations, callback failures,
TP send failures and board busy/full/continuation counts were zero.
Raw accepted plus raw dropped equals descriptor accepted plus descriptor dropped,
confirming both paths received the same offered frames.

At noisy 120 kHz, median TP emission was approximately 210,000/s with two
workers and 264,000/s with four. Descriptor populations varied, so these TP
rates do not establish a worker scaling factor. Keep two descriptor workers
for this configuration: four did not improve raw retention or maximum sampled
descriptor queue delay (approximately 24 ms in both profiles).

TP emission totals were 6,263,516 and 6,486,873; monitored consumer totals were
6,263,515 and 6,486,873. The one-count mismatch in the two-worker run remains
unresolved. Last monitored writer totals were 6,262,782 and 6,446,742.
The existing TPSet forwarder retains
a 100 ms horizon based on the newest TP timestamp; ending quiet input can leave
a tail unsent. The writer gaps require a separate flush audit before claiming
end-to-end storage losslessness. Live TA merging also remains pending.

[Steady counters](benchmarks/descriptor-workers.csv). Full board snapshots,
monitoring, whole-run counters and control logs are in `checks/tp-split/` on
the server; local copies and plots are in `daq-bandwidth/tp-split/`.
These short tests do not establish sustained throughput for dense light input.
