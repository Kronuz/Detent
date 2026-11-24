# consensus-next-deadline-removal review: correctness

Commit: consensus:d9c7747
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

None of the contributor's jobs had finished when I wrote this. A1 4ce8df, its fails-without
bcb45d, A3 d41f93 and its fails-without b8ca66 were still queued, and the suite c31551 was
running. I relied on nothing they claim. Before judging the tests, I rebuilt and ran
`consensus_core_deadline_test` myself (CI's Debug ASan+UBSan flags):

- At consensus:d9c7747: 35 checks, 0 failures.
- At consensus:d31b547, which has main's core.h: 8 failures. Each failure is one of the five
  cases' deadline or not-due checks: the removal, the removal at a full log, retirement after
  the compaction, the hand-off, the D4 quiet Tick, and retirement three times. So each case
  fails for its stated reason.

## What I checked myself

I built the Tick table from core.h before reading the doc's: step's `retire_log` and every
branch of `drive(tick)` (core.h:1459-1527) by role. The doc's 21 rows match mine; I found no
action on Tick whose time `next_deadline()` still omits. Receive and Propose drive nothing that
waits on a Tick, except the D9 hand-off, now reported. A leader's commit, and so D4 and the
pruning of removed replicas, always goes through a Persisted that drives (advance_commit
persists, core.h:1636).

Probes are in `.scratch/detent/review-consensus-next-deadline-removal-correctness/` (run.sh builds
a probe against a consensus checkout like the test):

- `plan_probe.cc` (R1): the Worker's turn order on a leader whose replacement lowered the group's
  `log_entries` and whose log was then compacted. The order is `retire_log()`, then `removal()`
  as the plan, then the Tick. At d9c7747, turn 0: 1047 retired at plan, the plan says no
  removal, and the Tick appends the Configuration: MISMATCH. At d31b547 the plan names the
  removal: 0 mismatches.
- `spin_probe.cc` (Q1, Q3): a host that only sleeps to `next_deadline()` and ticks then. The
  cases are formation, a learner joining, a D3 replacement, a D3 replacement under a lowered
  limit followed by compaction on every member, a witness founding, and a 2002-entry
  compaction. Result: 12 checks, 0 failures, 0 spins, 0 due Ticks that did nothing. Every
  removal commits and a data voter ends up leading. Retirement of the 2002 entries completes
  at the compaction's clock.
- `decisions.sh` (Q2): main's liveness harness, every Tick stepped, compiled against main's and
  the quest's `include/`. Over 20 seeds the outputs and TRACE hashes are identical in all four
  profiles: default 3257faeeda5b8893, arm64 ab75b965381ebe13, Linux e854b3b4b28c1b95, FreeBSD
  9c27aadf69552240. So the quest changes none of Core's decisions in the harness: who leads,
  what commits, when removals apply.

Answers to the lead's questions:

1. The table is complete (see above). A deadline reported too early would make a host spin;
   none was found by spin_probe. Every "due at once" condition is cleared by the Tick it
   calls for:
   - The removal goes busy once proposed.
   - The hand-off sets `transfer_`.
   - Each retirement step consumes at least one entry. The first item always fits the
     budget, and `validate_limits` keeps `command_bytes` above 0.
   - A follower's past deadlines move at their Tick.
2. The non-voter change is safe:
   - The election deadline is read only behind `is_voter` (core.h:345, 1466-1467).
   - Every way to become a voter resets the timer in the same step: an AppendRequest goes
     through `follower()` (core.h:1286, 682), an install goes through 786/809/819, and Start
     through 703.
   - decisions.sh shows the same decisions.

   The removal() fits-the-log condition does not change what Core appends, but it does break
   the Worker's plan (R1).
3. Retirement neither spins nor starves (R3 gives the bound).
4. The tests check values: decoded memberships, TimeoutNow to a data voter, deadlines equal
   to the clock, retired counts. They fail without the fix for the stated reason (see above).
   One helper can pass vacuously (R2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R1 | yes | defect | Gating `removal()` on `configuration_fits` makes the Worker's plan disagree with the Tick it admits. Sequence: (1) The Worker plans a leader's Tick from `removal()` after its turn's `retire_log()` (worker.h:503, 739-741). (2) `Core::step` retires another slice before `drive` asks `removal()` again (core.h:408, 1512-1513). (3) While the charged entries sit in [cap, cap+256) at plan time, the plan reserves nothing, but the Tick appends the removal. (4) `ingest` then throws "persistence lacks preowned reservation" (worker.h:1141) and the group fences. This can happen once a replacement or another change lowers `log_entries` below usage and a checkpoint leaves more than 256 entries to retire. That is the documented "a lowered retained-log limit blocks proposals until checkpoints" path. The change also contradicts deadlines.md's "`removal()` is the change the next admitted Tick proposes". | plan_probe at d9c7747: MISMATCH on turn 0. At d31b547: none. | Restore main's `removal()`: no fit check, so the Worker reserves and `propose_membership` refuses a misfit as before. Gate only the report: `next_deadline()` returns now for `removal()` when its entry fits. That check is exact, because retirement is reported first, so a full log still does not spin. Alternatively, gate `removal()` on the fit with retired entries excluded from the charge. Test: plan_probe's shape in core_deadline_test or worker_test (retire, plan, Tick; a Configuration append implies the plan named it), failing at d9c7747. Update the doc's removal paragraph. |
| R2 | no | gap | `quiet_before` returns without checking when the deadline is at or before the clock (core_deadline_test.cc:134-136). `d3_removal`'s last check and `d4_handoff_flights` would therefore pass vacuously if `next_deadline()` regressed to "due now" and stayed there. That is the spin the quest must not introduce, and nothing in the suite checks for it here. | Code read. spin_probe shows no spin today. | Where a case expects a future deadline, check `deadline > at`. Or add follower_read_test.cc:192's "a deadline stays due after its Tick" check to a sleep-to-deadline loop. |
| R3 | no | nit | "Each step retires 256" and "ceil(debt/256) turns" overstate the rate in one direction and understate it in another. A slice is also bounded by `limits_.command_bytes` (core.h:174, retirement.h:34-42), so entries above half of `command_bytes` retire one per step. Meanwhile a Worker turn retires twice (worker.h:503 and core.h:408). The real bound after a compaction is about log_entries/512 + 2·log_bytes/command_bytes turns. Each turn makes progress, and the Driver yields between turns (driver.h:796-806 breaks, then arm() re-arms). | Code read. spin_probe's 2002-entry compaction retires at one clock. | Reword deadlines.md and the journal's ruling. |
| R4 | no | nit | A fenced Core still retires on a Tick (core.h:408 runs before the fenced return) while `next_deadline()` says `maximum`. This contradicts the contract on paper only: fencing is terminal, and `Worker::next_deadline` is `maximum` when fenced. | Code read. | Optional: put `retire_log()` after the fenced check, or name fenced in the doc's "nothing" row. |
| R5 | no | gap | Not run. When the Worker refuses a due Tick with Pressure, `arm()` re-arms at Core's past clock (driver.h:832-846). So a due removal under control-pool pressure is re-stepped on every wheel pass until the pressure clears. A follower's past election deadline already behaves this way on main; this quest adds the removal and the witness hand-off to that class. | Code read only. | Driver-side: back off by `retry_ns` after a Pressure turn. That is outside this quest's file set; the lead decides whether to file a quest. |

## Not run

- I did not wait for the contributor's jobs (A1 4ce8df/bcb45d, A3 d41f93/b8ca66, suite
  c31551), which were queued or running at writing, nor for CI.
- I did not run held.sh (A3). decisions.sh covers stepped main against stepped quest only.
- I did not run a Worker or Driver reproduction of R1's fence. The Core-level sequence is
  probed, and the throw is read at worker.h:1141.
- I did not test R5.
- I did not check that spin_probe's witness case had the witness lead first: it reported
  `witness led first: 0`. core_deadline_test's d9 case covers that path.
- No sanitizer builds other than the probes' local ASan+UBSan Debug.
- This reviewer ran as a subagent, not a session titled `detent · reviewer
  consensus-next-deadline-removal correctness`.
