# namespace-s1-s2: Namespace S1/S2, grouped file syncs

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s1-s2`.

## Outcome

A publication syncs in D11's grouped order through completion's `SyncFiles` and `OrderFiles`: owner
records, barrier; payloads, barrier; the descriptor fsynced, renamed over `selected`, the directory
synced, then `change` incremented with no sync. The census issues no per-file sync and one startup
barrier, with `ready()` after it. Sync counters reach tests, the agent log and the relay status.
Detent pins `completion:560c288` with a consistent journal and consensus closure. Measured on
2026-11-07 (to repeat as jobs): 5M cold bootstrap 999-1001 s to 545 s, restart 61.9-65.9 s to
0.46-0.97 s, device flushes 5,777 to 27.

## Acceptance criteria

Numbering cites `namespace/s1-s2:docs/status/namespace-design.md` (S1 n, S2 n, "All four") and
`namespace/s1-s2:docs/status/namespace-s1-s2.md` ("Next n"), and the handoff step 4.

- A1 [artifact] S1 1, 1a, 2, 3: completion's `SyncFiles` and `OrderFiles` primitives, read-only opens, per-platform recorded sequences and their docs, on completion main (`completion:0490a5a`, `df6394a`, `560c288`; CI 41925122896 green on 9 jobs): `docs/status/namespace-s1-s2.md`
- A2 [artifact] S1 4, library half: the s1-s8, t1, t2 mutants (run 41925127096), separate sanitizer builds, completion CI green: `docs/status/namespace-s1-s2.md`
- A3 S1 4, Detent half: the manifest pins `completion:560c288` with `journal:e5f6ff1` and `consensus:ab05507` (D24), and `tools/check_pins.py` reports 0 mismatches: the `pins` gate
- A4 S2 1: publication syncs in the grouped order through S1's primitives; revert check: renaming before the fsync fails T7: `test_local_publication_power_cut` and its revert job
- A5 S2 2: the census issues no per-file sync and one startup barrier, `ready()` after it: `test_local_paged_store_startup`
- A6 S2 3: sync counters (files synced, device flushes, directory syncs) per publication and per startup, in tests, the agent log line "publication synced ..." and the relay status JSON: the `native` gate and a harness job reading both
- A7 S2 4: T7 interrupts every primitive of publication and of the startup barrier, plus restart-then-republish cycles, then simulates power loss; the per-file-order revert and the three T7 mutants of Next 3 fail it: `test_local_publication_power_cut` and mutant jobs
- A8 The lifecycle fix `Detent:0cd1527` and the T7 accounting fix `Detent:522452a` each have a passing positive run and a failing revert (ledger log, "recovery"): jobs
- A9 S2 5: restart and cold bootstrap timed at 10K and 100K under `exclusive`, before (`Detent:327ca58`) and after, with an A/A; 5M by the lead's ask: `exclusive` jobs, one arm per job
- A10 Sanitizers in separate builds on the power-cut, disk-store and artifacts tests (Next 4): the `asan` and `tsan` gates
- A11 [artifact] How-it-works note with the measured flush count (All four): `docs/mechanisms/sync-groups.md`
- A13 [manual] The ext4 VM result (1,445 files: per-file 4,300-4,438 ms, `IO::sync_files` 717.6 ms, threads 919.5 ms, `syncfs` 506 ms) is in completion's `docs/PLATFORM_FINDINGS.md` (handoff 4.4): the lead names the completion commit

CI on every job, pin-check included (Next 5, All four), is the landing's CI gate (REPORT O2).

## Contracts

Consumes completion's `SyncFiles`, `OrderFiles` and `PreparationFilePolicy::Stage`
(`completion:560c288`). Provides `DiskStore::sync_stats()`, `include/detent_local/sync_counters.hh`,
the agent log line "publication synced N files in N groups", the relay status fields `sync_*` and
`publication_*`, and D11's publication order on today's store. `dense-sync-stack` builds on it.

## Decisions

0. D32 (2026-11-09): the perf table (former A12) is quest sync-groups-perf; write A11 before the final job round, and run the A9 arms `--after` the four gates.
1. D24: the journal and consensus completion declarations are on their mains; this quest only
   takes the pins. The general pin wave stays in `pin-wave`.
2. Lead-accepted design-vs-code items for the reviewer: windows of at most 128 open files, one
   `SyncFiles` per window (windows + 1 flushes); no `.tmp-` names until S7; owners and lease in
   group A; device flushes derived by platform contract; cancel acted on at a window boundary.
3. Measured on 2026-11-06 and 11-07 (`docs/measurements/sync-groups-20261107/`, from the run queue, not campaign jobs):
   222/222 ctest, ASan and TSan 10/10 with 7,186 power-cut cases each; ext4 batching is 6 times
   faster than per-file and faster than concurrent threads, so no thread pool. A9 reruns the
   timings as jobs (REPORT O8).
4. The review is the architect's `durability` lens (handoff 4.5).
5. Open (lead): A13 as `[manual]`, or a `--repo ../completion` quest ordered `--after namespace-s1-s2`.
6. The cold-bootstrap harness must regenerate the 1-day TLS fixture
   (`tests/detent_authority/tls_certificates.cmake`, `-days 1`) before each arm: a stale copy stalled
   the 2026-11-06 r2 and r3 runs with no leader elected. `stage-noproof.sh`'s `fresh_fixture` does it
   (`docs/measurements/sync-groups-20261107/`); A9's jobs use that harness or commit one that does
   (the overseer's open item, `overnight-notes-r2.md`, "Still open for the lead").

## May land early

None.
