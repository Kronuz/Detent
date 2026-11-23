# logger-on-wheel review: correctness

Commit: logger:67391e63f8f4bfc4edaf8a4da9f6b24285c32b84 (67391e6)
Verdict: APPROVE
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261112-140315-039e6c (suite, Release, be2106cd8e43): 13/13, including logger_deferred and
  logger_deferred_stress. The two later commits touch only CHANGELOG.md and docs/perf/.
- 261112-140328-3415e1 (test/deferred_mutant.sh): the early (-3 ms) and late (+3 ms) arm mutants
  are both killed, so test/deferred.cc's never-early and lateness checks are live.
- 261112-140332-797330 (TSan over the suite, be2106cd8e43): 12/12.
- 261112-140421-86b4c3, -140412-ee682f, -140415-a16bfe, -140418-0c17c8 (A3, exclusive): read their
  logs. No row is past 1.10.
- 261112-140334-59109a (A4 grep).

## What I checked myself

The scripts are in `Detent/.scratch/review-logger-on-wheel-correctness/`: `probe.sh` and
`probe2.sh` (jobs), plus `shutdown.cc`, `race.cc` and `long.cc`. Each job runs against logger main
(dce2538, the heap) as `--ref OLD`.

- **Port fidelity.** `git diff main:bench/wheel_ab/d/logger.cc 67391e6:logger.cc` differs from
  arm D only in the removed `WAB_STATS` blocks and one comment. logger.h differs only in its
  comments and its include (`thread.hh` instead of `scheduler.h`). `claim()`, `run()`,
  `operator bool` and `cancelled()` match the old `ScheduledTask` one-shot line for line.
- **Lifetime and shutdown (risk 1), job 261112-144834-9ca932.** The shutdown probe's records are
  1. due now,
  2. due at 30 ms, 300 ms, 2 s, 5 s and 60 s, with handles dropped,
  3. cancelled at 30 ms and 5 s,
  4. kept at 30 ms and 5 s,
  5. watchdogs (`clears`) at 30 ms, 300 ms and 5 s.

  Under both `finish(1)` and `finish(10)`, the old and new loggers write the same lines and drop
  the same lines. After the handles go, `pending_count()` returns to 0, so every filed record was
  released, upper-level ones included. `clear()` after shutdown returns true on both. The diff step
  failed only on the `~309.9ms` vs `~302.5ms` age decoration, which is timing. probe.sh now strips
  that decoration before the diff.

  The race probe uses 8 producers. Each line is due 0.5 to 3 ms out, and the producer clears it,
  drops it, drops it as a watchdog, or keeps it, at a random point from before to after its due
  time. It checks that a won `clear()` is never written, that no line is written twice or early,
  that every kept or dropped line is written once, and that each thread's lines keep their order.
  It passes in Release, in Debug and under TSan, and passes on the old logger too.
- **Thread ownership (risk 3).** `wheel_` is touched only in `file_deferred`, `pass`,
  `wheel_next`, `earliest`, `close` and the loop, all on the LOG thread. `close()` resets it on
  that thread. `LogWorker` is never destroyed. `arm_remote` is unused: producers reach the LOG
  thread only through their rings. The same job ran the whole suite and all three probes in
  Debug, where wheel's owner `assert` is live. All passed. The evidence jobs ran in Release and
  RelWithDebInfo, where that assert is compiled out.
- **Ordering and timeliness (risk 2).** `expire` hands back only entries with `deadline <= now`.
  A line filed during a pass goes straight to the batch only when `due <= pass_now`. The batch is
  drained before `pass` returns. The slack predicate `due - 8 ms < worker_wake` is a superset of
  the old `due < worker_wake`, so it can add wakes but never remove one, and the `hot` rule only
  shortens sleeps. A line the slack does not wake for is filed when the LOG thread wakes, at least
  8 ms before it is due.

  Job 261112-145358-3d97b9 ran the long-delay probe three times on each logger. It sends lines
  across the 4.096 s level-0 boundary and out to 8-9 s. All about 42.5k same-thread ordered
  pairs hold, and no line is written twice or early. Isolated far lines are late by up to
  9.5-10.8 ms on the wheel and 9.8-10.0 ms on the heap. That is the macOS timed wait on multi-second
  sleeps, not the wheel, which adds about one tick (D86).
- **Behavior versus the old logger (risk 4).** The shutdown and cancel semantics are the same (as
  above). For the API and include change, see F5.
- **A3 label swap (risk 5).** `wheel-port.sh` labels arm D's binary (`logger_wheel_ab_d
  --backend D`) as A and A2, and the library (`logger_wheel_ab --backend A`) as D.
  `summarize.py --paired` keys runs by the `run round= arm=` label, not by the JSON's
  `backend`. So r = library / arm D. Both binaries come from one build of one `ab.cc`, and
  backends A and D make the same calls; only B differs. See F3 for the wheel pin.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | nit | The batch key's call time has two sources: `created` from the TaskRecord for a line filed straight into the batch (logger.cc:995), `atom_created_at` for a line from the wheel (logger.cc:1034). They are equal today, because every deferred `add()` passes `created_at == now` and `add` is private. The old `seq` tie-break is gone, and `std::sort` is not stable (logger.cc:888). Two same-thread lines with equal (due, created) could swap, but that is unreachable at the clock's resolution. | logger.cc:888, 995, 1034 | Read `atom_created_at` in both places, or carry `created` through. Optional. |
| F2 | no | gap | Flake risk: test/deferred.cc does `CHECK(h.clear())` on lines due 2 ms out (lines 171 and 217). A producer preempted for 2 ms or more between `do_log` and `clear()` aborts the test. That is plausible under TSan, or at nice 19 on a loaded host. | test/deferred.cc:168-171, 212-217 | Count `clear() == false` as "written at most once" (as race.cc does), or raise the cancel floor to around 20 ms. |
| F3 | no | nit | The journal says A3's library arm used wheel bfd72bc. bench/wheel_ab/d/CMakeLists.txt declares wheel 45f98eb first, and FetchContent's first declaration wins, so both binaries used 45f98eb. `wheel.hh` is byte-identical between 45f98eb and bfd72bc (`git diff 45f98eb bfd72bc` touches only bench/), so A3 stands. | bench/wheel_ab/d/CMakeLists.txt:7-10 | Correct the journal line. |
| F4 | no | nit | ARCHITECTURE.md:151 says the two sleep rules mean "the batch takes no inserts". The direct-to-batch insert exists (logger.cc:994-998), and the previous paragraph describes it. The rules make it rare, not absent. | ARCHITECTURE.md:151 | "rarely takes an insert". |
| F5 | no | nit | User-visible narrowing, which the CHANGELOG states. `Logging` is no longer a public `ScheduledTask`, so `operator bool`, `cancelled()` and `wakeup_time` are not reachable through `Log::release()`'s `shared_ptr<Logging>`. logger.h also no longer brings in `scheduler.h`. A grep of Xapiand, Detent, prism and http-log finds no use of either; they use `clear()` and `clean()`, which stay public. | logger.h:560, 609-614, 50 | Owner's call: keep as is (recommended, greenfield), or re-expose `cancelled()`. |
| F6 | no | nit | Outside this quest: logger still links `scheduler::scheduler` PUBLIC, for threadpool's `thread.hh` and test/frozen. D72's retirement of scheduler still counts logger as a user. | CMakeLists.txt:54 | Lead tracks under D72. |

## Not run

- Linux, FreeBSD, and the ASan and UBSan builds, which belong to CI. The branch has no CI run yet.
- Lines deferred past the second upper level (more than 4.4 min): wheel's own tests cover the
  cascade, and logger refuses delays of 24 h or more.
- A3: I reused the four exclusive jobs and did not rerun the numbers.
- Exceptions thrown from a handler mid-batch: the LOG thread dies either way, as it did before.
