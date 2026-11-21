# loop-idle-waits review: correctness

Commit: Detent:956a026f6a414daa8ae2e4f7b0004558b76761eb
Verdict: APPROVE
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 5, a delta review of Detent:dbea4a53..956a026f from a fresh context. Main's merges are ignored
except where they meet the quest. The range has two code changes. Detent:09ce7216 fixes F13 in the
authority's stop loop. Detent:563c5c31 (R12) replaces the relay host's sweep with main's per-turn
shares plus one sleep after an idle round. It supersedes Detent:59206705, which is reverted. The rest
is benches, the fixture port (Detent:219ba475, reworked by 563c5c31) and the journal. Authority code is
the same from 09ce7216 to 956a026f (`git diff --stat 09ce7216 956a026f -- src/detent_authority` is
empty). The four relay files at 956a026f are the same as on the bisect commit Detent:0e6eeae6.

F13 is fixed, and its fails-without bench fails. R12's round is complete, and I found no path that
makes work without either counting as progress or raising a wake, apart from F2's snapshot install
(carried, bounded at 100 ms). No new blocking finding.

## Evidence I relied on

- 261120-225535-9724ee, -225537-795628, -225539-3cb98d: three voters, 100k keys, SIGTERM to exit, at
  Detent:7d1fad45 (same authority code as head). The slowest stop is 1.252/0.227/0.569 s at head
  against 1.039/0.936/0.684 s at main, so all pass.
- 261120-225542-b03235: mutant stop-peers-sleep (the dbea4a53 condition). Head is 9.494 s against main
  0.523 s, so the job FAILs as required. This is F13's fails-without.
- 261120-161445-1fefcf, -161446-fa7216: the start-stop-sleep and stop-sleep mutants fail
  test_authority_restart_wake. The single-voter stop is still covered.
- 261121-050501-975e09: suite at 956a026f, exit 0. 261121-050502-92821b: relay tests, 43/43.
- 261121-050505-7960c4: test_relay_network_100000 x20 at head, 20/20, peak_connections 500 (one 501).
  261121-044608-6c41ed: the same test at main 3c04e6c1, passed, peak 500.
- CI 44907046261 at 956a026f: every job green, macos-arm64 included. CI 44907033869 at 0e6eeae6:
  macos-arm64 passed 3 of 3.

## What I checked myself

All of this is code reading at 956a026f. Copies are under
`.scratch/detent/review-loop-idle-waits-correctness/` (`r5_service_head.cc`, `r5_service_main.cc`,
`r5_relay_service.cc`, `r5_coordinator.cc`). I submitted no new job: the existing jobs and CI answer the
machine questions.

### Risk 1: F13's fix and other loops with peers

The stop loop (service.cc:3421-3436) yields when `!active && !history_active && (!peers || retiring)`.
With peers, that is exactly main's rule `owned_node && !drained() && !active && !history_active`
(r5_service_main.cc:3314). Without peers, it yields whenever no I/O is in flight, which is
39e5ab7e's single-voter rule. Measured stops match main (above). Nothing with peers sleeps long:
`wake_by` caps every authority sleep at 1 ms when `peers` is set (service.cc:1533-1535, 3294). Start
and replay with peers sleep at most 1 ms per visit, as main does at every visit (main 2030-2031,
2043-2048). Replay yields whenever main does (reclamation pending) and also when the applied index
moved.

### Risk 2: can a round end short?

Not for requests. `rescan()` sets each lane's `unswept` to its whole `slots.size()` when any slot is
occupied (service.cc:372-378). Every one of the 8 or 32 visits a turn makes debits one
(service.cc:401) while the cursor moves by one, modulo `slots.size()`. So a round covers every index
once, whatever the cursor's start. A lane that is empty at the rescan gets 0. A request that fills it
later raises the wake after storing the slot under the mutex (service.cc:588-601). The next
`begin_turn()` returns true and rescans (host.cc:398-400). A raise during a turn is not lost:
`arm()` expires at once (owner_loop.hh:44-47), and a post-path turn sees it in `begin_turn()`. A turn
with progress sets `round` and rescans (host.cc:477-479). So a long sleep needs every turn since the
last rescan to have no progress, every slot visited, and no bundle cleanup pending (host.cc:480-486).
That is the condition R12 states. Sessions can end short, slightly (F16).

### Risk 3: paths that make work

- A new request raises the wake (service.cc:600).
- Reply pump writable or settled events and mirror answers raise the wake (service.cc:575, 584).
- A slot freed or any fingerprint move counts as progress (service.cc:409-422).
- Every sink call marks `cleanup_pending` and raises (host.cc:96-99). The sink calls run on the owner,
  in the coordinator's coroutine, not in its worker lambdas (r5_coordinator.cc:158, 521-528,
  1058-1106). Only `select_publication`'s `gap()` and `fence()` make stale edges
  (update_cache.cc:12-18, 128). Both reach the host through the sink. `install_snapshot` leaves the
  lineage alone, so dropping main's every-turn cleanup loses nothing.
- Session open and close are requests in lanes 0 and 2. Their completion is progress, so the round
  over sessions that retires a closed session starts after it.
- A snapshot install is the exception. It is neither progress nor a wake (F2).
- Deadlines (poll waits, session expiry) are time-gated and seen at the next round, at most one
  health turn later (R3).

### Risk 4: the fixture's idle round

relay_service_fixture.hh:121-126 rescans, steps until `swept()`, and rescans again on any progress.
Then it requires `swept() && !step()`. Its exit condition is the host's sleep condition for
ServiceRuntime: a full round with no progress, plus one more idle step, which is stricter. It cannot
pass while a visited task still changes. It leaves out host-level state (bundle cleanup, snapshot), and
no test runs host.cc's turn (F3).

### Risk 5: `high_water >= 500`

Not introduced by the quest, and not blocking. test_relay_network_100000 drives `runtime->step()`
itself every 1 ms (relay_network_fixture.hh:274-290), not the host. `step()` visits the same lanes, in
the same order and with the same shares as main. Against merge base 647c71d8, the diff only adds
progress accounting. The fixture is unchanged from main, and main also sits at 500 (6c41ed). 500 is
the subscriber count, so the check asserts that all 500 connections are open at once. That is a
timing property of main's test under load (F18).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F13 | no | defect | Closed by Detent:09ce7216. A multi-voter stop yields through retirement as main does, and its stops match main's. | service.cc:3430-3436; 9724ee, 795628, 3cb98d; fails without: b03235 | None. |
| F14 | no | nit | Closed. The comments at service.cc:1536-1538 and 3430 now name the peers poll. | service.cc:1536-1538, 3430 | None. |
| F15 | no | gap | F13's guard is a bench (`compare.sh stop`, 100k keys, minutes per round), not a suite test. A regression of the stop rule is silent unless someone runs it. test_authority_restart_wake covers only single-voter stops. | tests/detent_delivery/mutants/loop_idle_waits.sh stop-peers-sleep | Optional, owner's call: a small multi-voter ctest that counts stop-loop sleeps against retirement visits, which needs no timing. Or keep the bench. |
| F16 | no | nit | A round over sessions can end before it visits every slot. While a retirement resumes on the same slot, `collect` spends one token a turn without moving the cursor, and `unswept_sessions` is debited for it. So a multi-turn retirement shortens the lap. The unvisited sessions wait for the next round, one health turn (100 ms) at most, for their expiry. Nothing is lost. | service.cc:426-429; session_table.hh:337-372 | Debit only the slots the cursor passed. Or leave it. |
| F17 | no | nit | The journal reuses "F13" for the macOS arm64 runner loss ("F13 NOT fixed by 59206705", "F13 ... fixed by R12", Blocked on). This review's F13 is the multi-voter stop, fixed by 09ce7216. R12 answers the arm64 hang (F4's CI read). | journal lines 34, 188, 230 | Name the hang by its own label in the journal. |
| F18 | no | gap | test_relay_network_100000's `high_water >= 500` asserts that all 500 subscribers are connected at once. That is a timing property, not a value of the code under test. It is main's test, and the quest leaves `step()`'s visiting and the fixture as main's. | relay_network_fixture.hh:330; 7960c4, 6c41ed | Owner's call: keep it, or print the peak and assert only bytes (review.md: a timing-dependent check reports and never fails). |
| F2 | no | defect | Carried, sharpened. A completed snapshot step installs the snapshot but counts as no progress (`progress \|= !complete && ...`), and nothing raises. A request waiting on it is seen after the round ends and the long sleep, 100 ms at most. | host.cc:426-434 | `progress \|= work.tokens != 128 \|\| work.bytes != 8192;`. |
| F3 | no | gap | Carried. The fixture's idle round matches ServiceRuntime's sleep condition (risk 4), but no relay test runs host.cc's sleeping turn: the bundle settle, the wake-to-rescan path or F2. | relay_service_fixture.hh:121-126 | As in round 1. |
| F4 | no | gap | Closed. CI 44907046261 at 956a026f is green on every platform. bisect/liw-fix 44907033869, with the same relay files, passed macos-arm64 3 of 3. | CI | None. |
| F10 | no | defect | Closed by Detent:8d56b21a. cf70b6 fails at d7de180b as required. | cf70b6 | None. |
| F11 | no | gap | Carried: test_owner_absence pins the rule but not its wiring. | service.cc absence wiring | Optional, as in round 3. |
| F12 | no | nit | Closed. The A1 Result row carries both intervals (75f254 and e93ac9). | journal Result | None. |
| F1 | no | defect | Carried, unchanged: a lone voter's first election can wait for the 250 ms health sleep. | service.cc service loop | As in round 1. |
| F6 | no | nit | Carried: read "no wakeups" from `interrupt_wakeups_per_s`. | idle_loops.py | As in round 1. |
| F7 | no | defect | Closed (round 3). | 3b65cd | None. |
| F8 | no | nit | Closed (round 3). | test_coordinator_teardown.cc | None. |
| F9 | no | gap | Closed (round 3). | 8e4934 | None. |

## Not run

- No new probe job. Every risk was answered from code and the existing job records. F16 and F18 are
  timing questions that need no machine to settle.
- A3 and A1 at head (1804d3, 75f254, e93ac9) and the TSan jobs: I read them only through
  `campaign status`, which lists them as met. I did not re-derive their numbers.
- The arm64 hang's mechanism (an advance rate that fills socket buffers, per the journal) is still a
  hypothesis. R12 avoids it by restoring main's rate, and CI is the oracle. I did not investigate it.
