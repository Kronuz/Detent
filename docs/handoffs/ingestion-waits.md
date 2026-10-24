# Ask: separate durable-ingestion syscall time from queue and request waits

**Priority:** P1 · Rank 3 of 7 · Ingestion-qualification blocker.

Start early: exact wait attribution is needed before changing durability or accepting ingestion throughput at release scale.

## Repositories and starting point

1. **Start repository:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
2. **IO diagnostics repository:** [Kronuz/completion](https://github.com/Kronuz/completion).
3. **Existing evidence:** [Kronuz/Detent/blob/main/docs/perf/seeding-waits.md](https://github.com/Kronuz/Detent/blob/main/docs/perf/seeding-waits.md).
4. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** `tests/detent_system/authority.py`, `writers.py`, `mixed.py`, authority request phases under `src/detent_authority/`, and completion's existing timing hooks. Baseline: 100K records seed in 104.338 s, four producers and 784 transactions of up to 128 mutations. A verified 10K follow-up takes 10.363 s; its sampled sync occupancy is 5.963 s but misses the final receipt.

**Dependencies and ownership:** Can start now. Coordinate completion instrumentation with its owner and resource-adoption changes with that task. No journal-format or durability-contract changes during attribution.

**Release-unblocking outcome:** A reproducible seed-boundary phase table distinguishing syscall time from queue/reaping delay and overlapping requests, plus verified restart. Propose and qualify any justified fix separately; report the remaining throughput limit rather than declaring diagnosis alone a performance fix.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Investigate a C++20 replicated service that acknowledges writes only after its required durability barrier. A load driver seeds fresh state through several concurrent producers. Each producer awaits its own bounded batches serially. Downstream distribution services start after seeding, so their work cannot explain this seed interval.

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
