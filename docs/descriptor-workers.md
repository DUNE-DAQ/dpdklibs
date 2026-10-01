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
sanitizers. The CERN release build and live rate scan remain pending; these
tests do not establish a hardware throughput limit.
