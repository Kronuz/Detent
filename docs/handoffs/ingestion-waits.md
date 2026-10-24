# Ask: attribute authority ingestion waits before optimizing durability

Work in `/Users/kronuz/code/Detent`. Detent has a replicated authority, downstream relays, and local agents. The capacity harness first seeds a fresh three-voter authority through four concurrent producers; only afterward does it start relays and agents. Each producer awaits its own transaction batches serially, with at most 128 mutations per batch. Seeding is an end-to-end publish workload, not a bulk-load primitive or maximum-throughput result.

## Existing evidence

Read `docs/perf/seeding-waits.md`, `docs/status/seeding-waits.md`, `benchmarks/system-capacity-20261023.json` and SC-4 in `docs/reviews/scaling-2026-10-23.md`. Frozen baseline `0c1e5ad` seeded 100K keys in 104.338 s (784 transactions), and 130K in 138.714 s. The accepted capacity targets are 1M, 5M and exploratory 10M; do not invent a 1.3M target from the enterprise reference.

The first post-seed leader health snapshot at 100K reports 804 publication cycles, 1,608 file-sync completions totaling 49.62 s and 804 directory-sync completions totaling 22.23 s. These are submission-through-reaping occupancy intervals and include some post-seed work. They are not syscall timings. Three-voter CPU totals also include startup.

A 10K follow-up applies 80/80 transactions in 10.363 s with full-state verification. Its health delta covers 140 file syncs and 70 directory syncs totaling 5.963 s, but misses the final receipt. Publisher and client timing histograms overlap concurrent requests; their sums cannot be subtracted as independent wall-time phases. An attempted DYLD syscall shim produces no valid timing and must not be reused as evidence.

The local journal append performs two file syncs and one directory sync. The fallback worker, queue notification and owner reaping all contribute to observed occupancy. The Python harness also creates a fresh TLS connection and sends `Connection: close` on every request (`tests/detent_system/authority.py`). Neither that harness behavior nor relay rebuilding has been proven the seed bottleneck.

## Requested work

1. Recover the existing scratch probes and frozen binaries before writing new tools. Add narrowly scoped diagnostic timing for actual file/directory sync worker execution, worker queue delay, notification and owner reaping. First check whether updated completion already offers the needed timestamps; extend its existing mechanism if necessary.
2. Add correlated request phases: client send, server admission, proposal acceptance, durable/commit progress and response. Capture exact seed boundaries and explain clock domains. Record checkpoint counts and bytes during growth.
3. Repeat the existing 10K workload under exclusive host holds. Verify every definition, durable acknowledgment, restart and recovery. Report spread, source/dependency identities, overhead of instrumentation and raw artifacts.
4. Recommend an optimization only after attribution. Any grouped durability work must preserve the existing acknowledged durability frontier and replay semantics; get a component design review before changing that contract. A persistent-connection harness arm must be reported separately from unchanged-baseline behavior.

## Constraints and delivery

No weakened sync, early acknowledgment, changed journal bytes, silent retry or unbounded queue. Do not add a parallel IO mechanism. Read repository `AGENTS.md`, `docs/SHARED_LIBRARY_DECISIONS.md` and sibling `~/code/Kronuz/cpp-libraries/AGENTS.md` before shared-library edits. Coordinate completion instrumentation with its owner and storage-task changes with the lead.

Use a separate worktree, `.scratch/` artifacts and serial investigation. Run `quiet check` before builds/tests; wrap them in `quiet light` with at most four jobs. Use `quiet exclusive` for published timing. Do not launch a costly capacity ladder to answer a 10K attribution question.

Return a phase table distinguishing measured syscall time, queue/reaping intervals and overlapping request latency; isolated diagnostic/fix commits; raw paths and restart verification. State what remains unmeasured. Do not push shared-library changes or pin updates without lead coordination. Cost or power estimates require an explicit deployment model.
