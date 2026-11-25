# consensus-recovered-over-limit-start review: correctness

Commit: consensus:d03a71b
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The code is right: the Driver path cannot loop, starve, run after Start, or fence a store that
would have started, and A2's tests check values and fail without the fix for the stated reason.
The one blocking finding is the A1 table: row 2 cannot reach the fence, and rows 3-4 reach the
Driver only together with row 1.

## Evidence I relied on

- 261124-201345-5a60e2 (`reclaims` at d03a71b, 17 checks, 0 failures) and 261124-201348-90beab
  (`refuses` at d03a71b, 12 checks, 0 failures).
- 261124-201346-931ecd (`reclaims` at consensus:0cf87ca, exit 1: "the open completes: never, within
  200 ms") and 261124-201349-16891d (`refuses` at 0cf87ca, exit 1: the open never completes, no
  `fenced`, no Sink fence). 0cf87ca is main (consensus:c724c58) plus the `over_headroom` name only
  (`git diff main 0cf87ca -- include/`), so both fail for the reason A2 states.
- The suite (261124-201351-4ac6e5) and CI were still queued when I finished, so I did not rely on them.

## What I checked myself

Probe scripts are under `.scratch/detent/review-consensus-recovered-over-limit-start-correctness/`.

- **Probe 261124-202348-6f464f** (`refill_order_probe.sh` / `.cc`, journal `Admission` at
  journal:9e11713): an owned store at its limit whose Control appends drained part of its control
  pool, before and after a crash. The pools keep their total, but refill moves it from replacement
  to control. A Replacement permit of R is admitted before the crash and refused after it:
  - With C 64 KiB, R 512 KiB and 48 KiB drained, the pools go from (16 KiB, 512 KiB) to
    (64 KiB, 464 KiB).
  - With C 256 KiB, R 64 KiB and 250 KiB drained, they go from (6 KiB, 64 KiB) to (70 KiB, 0). The
    114-byte pack is still admitted, so this row-2 shape starts and is not fenced.
- **Q2, loop and starvation.** `reclaimed_` latches on a complete pass (worker.h:503-506, 900, 947),
  and a pass ends when the directory cursor ends (journal reclamation.h:84-88). `turn()` handles an
  in-flight `maintenance_io_` or `io_job_` before it can return Pressure (worker.h:516-532), so
  `start_maintenance_io` cannot double-start (worker.h:881). `finish_maintenance_io` sets
  `timer_blocked_` from `maintenance_owed_timer_`, which is false before Start (worker.h:940, 1234),
  so no Maintenance loop hides the Pressure. Each Driver step is bounded by `turn_budget`
  (driver.h:798).
- **Q2, after Start.** The Worker's `started_` is set where Start is admitted (worker.h:769). That is
  the same `admit` that sets `g.started` (driver.h:893-897). Start plans no appends
  (admission.h, the Start/Read/... branch), so it is never Pressure itself.
- **Q2, fencing a store that would have started.** Driver groups are always Host groups with async
  storage (driver.h:202, worker.h:117-124), with a per-group `Admission` (journal store.h:405-410).
  Before Start no permit is held, so a pre-Start Pressure is bytes or entries short in that ledger.
  Nothing but reclaim changes that ledger before Start, and at main such an open never completes
  (the fails-without jobs). No store that opened at main now refuses, so the quest's contract
  ("a store that opened before now refuses") is not triggered.
- **Q1, roots before Start.** No checkpoint, install or publication is admitted before Start
  (`admit` returns before calls, driver.h:893). The only pre-Start write is the initialization
  record, a Control append, which is refused or written before any reclaim job runs. One complete
  pass therefore examines every file against fixed roots. Retirement never returns Pressure
  (worker.h retirement_turn).
- **Q4.** `reclaims` checks by value that the staged image is gone and the published checkpoint
  kept, that the lone voter is Leader, and that "c" commits at its placed index. `refuses` checks
  `what() == over_headroom`, the `fenced` type, a single Sink `Fenced` with that reason, and the
  artifacts unchanged.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | A1 table and its summary ("rows 3-4 now start, rows 1-2 fail by name") are wrong about modes and end states. (a) Driver groups are always Host groups (driver.h:202). A Host group's appends add nothing (`append_plan` returns `added = {}` when shared, journal journal.h:136; settled at store.h:78), so its control pool is whole whenever no permit is held. Row 2 therefore never reaches the fence. (b) Row 2 exists only for owned Workers, which have no Driver. Those hosts get an unnamed Pressure after one pass, the same stall as main. Detent's authority submits Start with no pre-Start Tick (service_runtime.cc:871-873, node.cc:208), so it starts. The probe's second case (70 KiB control after recovery, pack admitted) starts even through a Tick. (c) For Host groups, rows 3 and 4 put the store over its headroom only together with row 1. A kill mid-checkpoint leaves at most the outstanding Replacement peak, which was already funded under a whole control pool. Row 4 says "both" without the caveat row 3 has. The test is itself row 1 plus row 4 (roomy, then a lowered `hard`). The deadlines.md section repeats (a) by listing "a pool drained by Control appends before a crash (an owned store's log)" as a way into the Driver's rule. | driver.h:202; journal:9e11713 journal.h:136, store.h:78; recovered_over_limit_test.cc `roomy`/`lowered`; probe 261124-202348-6f464f | Correct the table and docs/mechanisms/deadlines.md. For a Driver group, only a limit lowered (or a control pool raised) between runs lands above the headroom, and crash leftovers only add to it. Row 2 is owned-only and never fenced: a Worker host sees Pressure after one pass. Remove "rows 1-2 fail by name": row 1 starts when a pass frees enough (the `reclaims` test is row 1). No code test: A1 is a manual row the lead re-reads. |
| F2 | no | defect | Q3, stated plainly: no crash turns a working store into one that cannot reopen. A crash can turn an owned store that could compact into one that cannot. Journal's refill order (control before replacement) moves the replacement pool's bytes into control when the census rebuilds the ledger. A store whose Control appends drained more than R minus the checkpoint peak reopens with a replacement pool too short for any checkpoint. Its Normal work is refused (`normal_ready` needs whole pools), and with free at 0 nothing ever refills it, so it is wedged for good. Before the crash, its whole replacement pool funded compaction. Driver (Host) groups are unaffected (F1a). The defect predates this quest and lives in journal admission.h `refill`/`Admission` ctor. The contributor flagged it as "Adjacent". | probe 261124-202348-6f464f (both cases); journal:9e11713 admission.h `refill` | A journal quest. Options: keep pool ledgers across a crash, refill replacement up to its pre-crash share, or document it. Its test is this probe's two cases asserting that the post-crash Replacement(R) is admitted. |
| F3 | no | gap | The synchronous branch of the pre-Start reclaim (`reclaim_step`, worker.h:505) has no test. `recovered_over_limit_test` only drives Host Workers, which are always async. The branch is reachable from owned Worker hosts that turn before Start, such as Detent's authority `service()` before `start()`. | worker.h:505; driver.h:202 | Optional: an owned-Worker case that turns before Start over its headroom and expects Maintenance steps, then Pressure once a pass completes. |

## Not run

- I did not wait for the suite (261124-201351-4ac6e5) or CI at d03a71b; both were still queued or
  running. A4 is the lead's gate.
- No Driver-level run of an owned store. The Driver cannot host one (driver.h:202).
- Torn-header staged files, which reclaim counts as `unknown_files` and never removes, are
  pre-existing journal behavior outside this quest.
- This review ran as a subagent, not a session titled
  `detent · reviewer consensus-recovered-over-limit-start correctness`.
