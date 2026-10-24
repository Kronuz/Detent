# Ask: separate durable-ingestion syscall time from queue and request waits

Investigate a C++20 replicated service that acknowledges writes only after its required durability barrier. A load driver seeds fresh state through several concurrent producers. Each producer awaits its own bounded batches serially. Downstream distribution services start after seeding, so their work cannot explain this seed interval.

This brief names no application or repository. Confirm the supplied implementation's journal operations, timing boundaries and acknowledgment contract before interpreting results. Existing observations suggest storage completion occupancy is substantial, but do not establish time spent inside sync syscalls.

## Measurement traps to avoid

1. Submission-through-reaping timing includes worker queueing, syscall execution, notification and owner-executor delay. It is not syscall timing.
2. A health snapshot taken after seeding includes later work. A snapshot before the final receipt misses work. Record exact seed boundaries and counter sampling ages.
3. Concurrent request latency sums overlap each other and storage occupancy. Subtracting these sums does not isolate replication, HTTP or application CPU.
4. CPU totals may include startup. A new TLS connection per request measures a different client lifecycle from connection reuse. Neither should be silently treated as production peak throughput.
5. A timing shim that records no calls supplies no evidence, even if its loader reports successful interposition.

## Requested work

1. Reuse existing probes and binaries where source identities match. Locate existing IO diagnostics before adding counters. Extend the existing mechanism with worker queue delay, actual file/directory sync execution, notification and owner reaping intervals.
2. Correlate request phases: client send, server admission, proposal acceptance, durable/commit progress and reply. Explain clock domains and distinguish per-request latency from summed serial occupancy. Record checkpoint count and bytes during growth.
3. Repeat a small verified workload, such as 10K records with several concurrent producers and bounded batches, under controlled host load. Capture exact seed boundaries, measurement spread, source/dependency identities, raw artifacts and instrumentation overhead.
4. Recommend an optimization only after attribution. Report persistent-connection experiments separately from the unchanged client baseline. Grouped durability requires an explicit proof that acknowledged data survives the same failures as before.

## Constraints and acceptance

No weakened sync, early acknowledgment, changed journal bytes, silent retries or unbounded queue. Do not add a second IO mechanism. Shared-library changes extend existing diagnostics and follow that library's compatibility rules. Keep the journal publication sequence and durability modes unchanged during attribution.

Verify every resulting record against an independent oracle, then restart and recover the acknowledged state. Exercise an interrupted write and resource refusal when implementing an optimization. Preserve failed and interfered measurements. Distinguish source-established complexity from measured timing and unexplained residuals.

Work serially in an isolated checkout and coordinate builds/measurements through the host's load-control mechanism. Use exclusive host access for published comparisons. Do not launch a multi-million-record trial to answer a small-workload attribution question.

Return a phase table separating actual syscall execution, queue/reaping intervals and overlapping request latency; reproducible commands and raw artifacts; isolated diagnostic/fix commits; and restart verification. State what remains unmeasured. Cost or power estimates require an explicit deployment model.
