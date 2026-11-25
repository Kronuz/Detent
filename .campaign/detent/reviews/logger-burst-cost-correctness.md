# logger-burst-cost review: correctness

Commit: logger:7e261c14b4329d7e4820938b4ab7c7de6d34b51e
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261116-014628-44ad05 (exclusive A/B, 30 rounds, `bench/lg.cc`, at 7e261c1) and its A/A
  261116-014632-1037e9: every burst and back-to-back row at or below 1.10x (burst 1.012 and
  0.973, back to back 1.006 and 0.996, end to end 1.009 and 0.994), A/A within about +/-6%. A2 met.
- 261116-012408-fce080: the suite at 7e261c1, 15 of 15 (deferred, deferred_ties, wake, transcript).
- 261116-012402-971dec: `test/wake_mutant.sh` at 7e261c1: fixed tree 1 notify over 1,000 batched
  L_INFO lines, mutant (slack back on `ring_published`) 794, killed.
- 261116-012411-484c4f: the deferred mutants (early, late, ties) killed at 7e261c1. A3's D86 behavior.

## What I checked myself

Scripts in `.scratch/review-logger-burst-cost-correctness/` (home checkout): `probe.sh NAME [ORIG REPL]`
copies the tree, applies one mutant to `logger.cc`, adds `ordered_burst.cc` (1,000 `L_DELAYED(false,
500us)` lines, which are queue-order task records, then 1,000 deferred 20 ms lines, counting
`worker_notifies`), runs it, ctest, and `logger_deferred_test` three times with `DEFERRED_LATENESS=1`;
`probe.sh tsan` runs the suite under TSan.

| Job | Tree | ordered burst | deferred burst | suite | deferred lateness p99 |
|---|---|---|---|---|---|
| 261116-050407-f0f461 | fixed | 1 notify | 1 | 15/15 | 2.17, 2.76, 1.89 ms |
| 261116-050411-9d9685 | ordered tasks take the slack (`push_task` -> `due - kWakeSlackNs`) | **720** | 1 | **15/15** | 1.90, 1.82, 2.14 ms |
| 261116-050415-8c4f93 | deferred lines lose the slack (`push_task` -> `due`) | 1 | 1 | **15/15** | 2.31, 2.77, 2.56 ms |
| 261116-050419-ea9d04 | fixed, `-fsanitize=thread` RelWithDebInfo, macOS clang | | | 15/15, no report | |

Reading of the code (logger.cc:1219-1226, 1289-1291, 1445-1462, 1322-1349):

1. Routine lines. `ring_published` and ordered `push_task` wake on `due < worker_wake`, the f61d8df
   rule. `worker_wake` is the timed wait's actual deadline (or max, an untimed wait), so a line not
   woken for is due at or after the LOG thread's own wake: it waits for no slack, only for the OS
   timer's lateness, as at f61d8df. An at-once line (`due == created`) always wakes it.
2. Deferred lines. Unordered `push_task` keeps `due - kWakeSlackNs`; filing (`file_deferred`), the
   wheel, the already-due insert into the batch and the merge are untouched, so a deferred line
   cannot fire earlier or be lost by this change. A deferred line the LOG thread files late goes
   into the batch in order and is written in that pass.
3. Concurrency. The handshake (publish seq_cst, load state seq_cst; LOG thread stores
   `worker_wake`, sets kSleeping seq_cst, fence, second look) is unchanged; only the threshold moved,
   and a lower threshold drops only wakes whose line is due at or after the timed wait's deadline.
   The `fetch_and(~kSleeping)` still lets one producer notify per sleep. `end()` notifies
   unconditionally, so finish() is unaffected. The merge order of routine and deferred lines is
   decided in `pass`, which the fix does not touch. TSan is clean.
4. Docs. ARCHITECTURE.md (filing rule 2, the sleep handshake at lines 92-101 and 214-215),
   README "Sub-millisecond deferred timing", CHANGELOG and `docs/perf/burst-cost.md` match the code.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The fix's second half, queue-order task records (`push_task` with `ordered`) waking only on `due < worker_wake`, has no test: restoring the slack there makes a 1,000-line burst of such records notify the LOG thread 720 times (fixed: 1) and the whole suite still passes. These records are lines the fast path does not take (`L_EXC`, sub-tick `L_DELAYED`, a refused fast format), and `docs/perf/burst-cost.md` claims them. | 261116-050411-9d9685 vs 261116-050407-f0f461; logger.cc:1290 | Add an ordered-task burst to `test/wake.cc` (as in `ordered_burst.cc`: `L_DELAYED(false, 500us, ...)` under the held watchdog, `notifies < 50`) and a second mutant in `test/wake_mutant.sh` replacing `get().wake_for(ordered ? due : due - kWakeSlackNs);` with `get().wake_for(due - kWakeSlackNs);`; its job must show it killed. |
| F2 | no | gap | The deferred lines' slack, the part the fix keeps, is pinned by nothing: removing it passes the suite, and the deferred test's lateness with `DEFERRED_LATENESS=1` does not move (p99 2.3-2.8 ms vs 1.9-2.8 ms fixed). Present since a07f3fe, not introduced here. Owner's options: keep it as is; pin it with a test that shows a deferred line filed before due (for example a count of already-due inserts into the batch); or drop it if no load shows a benefit. | 261116-050415-8c4f93 | Owner's call; outside this quest. |
| F3 | no | nit | `worker_notifies` is a production counter for a test (a relaxed RMW on the notify path, which already takes a mutex and a syscall: cost negligible). It has no `alignas` and may share `worker_wake`'s cache line, which producers read on the slow path; it is written only when notifying. | logger.h:228 | Optional: `alignas(kCacheLine)`, or leave it. |
| F4 | no | nit | ARCHITECTURE.md rule 2 says a queued line "keeps the rule above (due before `worker_wake`)"; the rule above in that list is rule 1 (sleep one tick). The `worker_wake` rule is in the earlier section (lines 96-99). | ARCHITECTURE.md:163 | "keeps the producer's rule (due before `worker_wake`)". |

## Not run

- CI at 7e261c1 (Linux, FreeBSD, ASan/UBSan, Linux TSan): not pushed; my TSan run is macOS clang only.
- A1's bisect evidence: 261116-001349-19ec7c ran other code than 7e261c1 (the status line asks for a
  rerun); I did not rerun it. Its table is in `docs/perf/burst-cost.md`.
- No benchmark of my own; I accepted 44ad05 and 1037e9.
- No multi-producer burst of ordered task records, nor of routine lines under load beyond the suite.
