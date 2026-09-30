# Frame workers

`DPDKPortConfiguration.processing_cores` selects dedicated callback CPUs.
An empty relationship retains synchronous processing.
CPUs must be distinct from DPDK lcores; use the NIC's NUMA node.
`processing_queue_batches` defaults to 256 and must be a power of two >= 2.

RX parses frame boundaries and copies frames into bounded batches.
Each source stays on one worker; unused configured workers are not started. Every RX queue has a separate SPSC queue
for each worker, preserving source order without a shared producer lock.
A source mapped to multiple RX queues is rejected when workers are enabled.
Workers execute the existing callback, including raw buffering and TP extraction.
Queue storage is allocated and first touched on the worker CPU.
Each batch holds at most 16 KiB and 128 frame references; RX flushes at burst end.
Queue saturation drops incoming frames and increments an explicit counter.

`FrameWorkerInfo` reports cumulative enqueued, processed, dropped and failed
callback counts, current pending batches, lifetime high-water batches, and
maximum publication-to-processing delay since the previous monitoring report.
Stopping flow quiesces RX and drains workers before returning.
Scrapping also joins the processing threads.

`FrameWorker_test` checks copied-buffer ownership, concurrent source ordering,
queue saturation, callback failures, draining, and configuration rejection.
The worker pool changes the receive path; it does not add live TA integration
or an automatic source-throttling policy.

## CERN tests

Gateware `14f56c3`; continuation off; 16 connected channels; independent
10 ms DAQ windows at 10 Hz. Runs 45333–45337 used ten-second counter phases.
The board exposes two active streams, each carrying eight channels; this
configuration can use at most two callback workers. The initial four-worker
run had two idle workers, which the final implementation no longer starts.

Threshold 8 generates noise descriptors, including board-side descriptor
overflows rejected by the TP converter. Sampled transport losses were:

| Target kHz | Synchronous NIC loss | Two-worker NIC loss | Two-worker queue loss |
| --- | --- | --- | --- |
| 50 | 0% | 0% | 0.24% |
| 80 | 14.82% | 0% | 1.00% |
| 120 | 51.91% | 0% | 5.55% |

Quiet threshold-64 input at 120 kHz had 44.11% synchronous NIC loss versus
0% NIC loss and 4.42% queue loss with two workers. Board busy/full rejections,
callback failures, raw-buffer insertion failures, and TP-vector send failures
were zero in the sampled phases. Every enqueued callback drained before scrap.
These are short measurements, not an end-to-end losslessness qualification.

Increasing queue capacity from 256 to 4096 batches left 6.61% noise queue loss
at 120 kHz and raised sampled maximum queue wait to 535 ms. The test profile
therefore keeps two workers and 256 batches. Larger queues do not solve the limit.

Run 45338 profiled the callback workers at 120 kHz. Of 684 CPU-clock samples,
SkipList insertion accounted for 19.15% self CPU time; idle yielding was 26.46%.
The sample includes startup idle time and does not isolate steady-state stalls.
The raw request handler also retains ten seconds in its SkipList, independent
of the configured buffer size. The next stage must separate descriptor processing
from raw buffering and address stream/channel partitioning before live TA work.

[Sampled counters](benchmarks/frame-workers.csv).
Full board counters, monitoring, run logs and profiles remain in the server
work area's `checks/frame-workers/`; plots are in the local
`daq-bandwidth/frame-workers/`. Descriptor populations vary between runs.
