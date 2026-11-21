# loop-idle-waits review: correctness

Commit: Detent:dbea4a53faf43a404668ac7d6e5184f186068ad0
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 4, a delta review from a fresh context. Since round 3 (Detent:d7de180b), three commits touch
the quest's code: Detent:8d56b21a (bench naming, F10's fix), Detent:39e5ab7e (start, replay and stop
visit the node again at once unless I/O is in flight) and Detent:9b27cb95 (removes Detent:5f8b0ba1's
64-yield bound, so service.cc again equals 39e5ab7e's). The rest is journal, watchdog, samplers and
merges of main with no src or include change in the quest's files.

The single-voter change is right, and its test fails without it. One new blocking defect: `in_flight()`
counts `peers`, so a multi-voter stop now sleeps 1 ms after every budgeted retirement visit. Main and
the pre-39e5ab7e code yield there (F13).

## Evidence I relied on

- 261120-051929-b61395: the suite at Detent:9b27cb95 (= dbea4a53's quest code), 288/288, exit 0.
- 261120-051929-26bcd0: test_authority_restart_wake x10 at 9b27cb95, exit 0. Starts 23-235 ms, stops
  0-10 ms.
- 261120-013518-340c44, -021138-336caf, -051932-e37617: the start-stop-sleep mutant (`in_flight` always
  true) fails the test, exit 8. Starts 1.8-5.0 s, stops 250-501 ms.
- 261120-054330-7de7d0, -054343-67cb93, -054345-f92c9a, -054347-5f3f0d: test_relay_service a5 under
  TSan, 20/20 (F7's end-to-end witness).

## What I checked myself

Scripts are under `.scratch/detent/review-loop-idle-waits-correctness/` (`r4_probe.sh`,
`r4_stop_scale.py`, `r4_39e5ab7e_service.patch`).

- 261120-150702-7fe4aa (`r4_probe.sh revert`): 39e5ab7e's service.cc hunks are reverted exactly
  (`patch -R`). test_authority_restart_wake fails, exit 8: starts 461/1775/3278/5037 ms, stops 250 ms.
- 261120-150709-8ed582 (`r4_probe.sh stop`): only the stop loop goes back to its pre-39e5ab7e rule.
  The test fails, exit 8: every stop is 250-252 ms and every start is 24-193 ms. So the stop half is
  covered on its own, not only through the start half.
- 261120-160911-3db6fb (`r4_probe.sh scale 50000`): a three-voter SIGTERM-to-exit time at 50k keys,
  at head and with the pre-39e5ab7e stop rule. It was queued (load 74) when I wrote this, so F13
  rests on the code reading below. Its result settles F13's magnitude.

### Q1: a missed wake or an endless spin?

No lost wake. The three loops sleep only when `active || history_active || peers` (service.cc:1540).
`active` and `history_active` are cleared by the driver's completion callback, which then calls
`wake.raise()` (service.cc:1441-1442, 1463-1464). That callback runs on the owner. `raise()` is
sticky, and `arm()` expires at once if a raise came since `begin_turn()` (owner_loop.hh:31-47). So a
completion before the sleep, or during it, ends the sleep. With peers, `wake_by` caps every sleep at
1 ms. The set of sleeping states is no larger than before the fix, except in the multi-voter stop
(F13). A visit that has budgeted work while I/O is in flight waits for that completion (start, stop),
or yields if the applied index moved (replay). That wait is bounded by one I/O, so it is a wait, not
a lost wake.

No new endless spin. The yield path runs only with nothing in flight. `task::yield` reposts through
`asio::post` (task_asio.hh:77-81), so io handlers still run between visits. Each visit advances
budgeted work: the worker's `run_one`, `State::collect` (32 receipts) and `retire_actions`. I found no
time-gated wait in start, replay or stop (`WorkerLimits` is budgeted, not timed). A state that never
advances would hang main too, because main's loops have the same exit conditions. The yield bound
only priced such a hang (a 1 ms poll against a core), and 9b27cb95's measurement shows the bound did
not change the visit rate under load. Removing it is sound.

### Q2: does test_restart_wake fail without 39e5ab7e?

Yes. The exact revert fails (7fe4aa), the stop-only revert fails (8ed582), and the contributor's
broader mutant fails (e37617).

### Q3: are earlier findings reopened?

No. 8d56b21a closes F10: `git grep -i detentd dbea4a53 -- bench` is empty. The only remaining hits
are the dated revert scripts in tests/detent_authority, and those name BASE's paths on purpose. The
F10 fails-without job is 261120-144914-cf70b6 (queued). 39e5ab7e and 9b27cb95 leave alone the
absence wiring (F7, F11), the service loop's sleep (F1), the relay host (F2, F3), the coordinator
and every `raise()` site.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F13 | yes | defect | A multi-voter stop sleeps 1 ms after every retirement visit. `in_flight()` includes `peers` (service.cc:1540), and `peers` stays set through the stop loop (service.cc:3421-3434), so `wake_by` caps each sleep at 1 ms and no peer frame raises a wake. Retirement frees about 32 arena nodes per visit (`CommandWorkBudget` receipts 32, state.hh:134; `State::collect`, state.cc:846-889), so the stop costs about N/32 ms for N retained nodes: seconds at 100k keys, and about 30 s at 1M, against a timeout's SIGKILL. Main (and this quest before 39e5ab7e) yields while `owned_node && !drained() && !active && !history_active`. This was introduced by 39e5ab7e. Single-voter stops are unaffected. | service.cc:1540, 3421-3434; probe 261120-160911-3db6fb (queued) | In the stop loop, yield while the node has retirement work and no storage or history I/O is in flight, and sleep for peers only once the node is drained, for example `active \|\| history_active \|\| (peers && (!owned_node \|\| owned_node->drained()))`. Keep the drained-node case yielding when there are no peers, because test_restart_wake's 250 ms stop depends on it. Test: a three-voter SIGTERM-to-exit bound at a sizable state (r4_stop_scale.py is a starting point), failing at dbea4a53. |
| F14 | no | nit | The comment at service.cc:1536-1538 says only storage or history I/O sleeps, but `peers` also sleeps (1 ms). It should say so, or F13's fix should make it true for stop. | service.cc:1536-1540 | Reword with F13's fix. |
| F10 | no | defect | Closed by Detent:8d56b21a. The bench builds and runs detent-authority. | `git grep -i detentd dbea4a53 -- bench` | Read 261120-144914-cf70b6 when it ends. |
| F11 | no | gap | Carried: test_owner_absence pins the rule but not its wiring; only a5 sees the wiring, by timing. | service.cc absence wiring | Optional, as in round 3. |
| F12 | no | nit | Carried: the A1 Evidence row should carry both intervals' numbers. | journal Evidence | As in round 3. |
| F1 | no | defect | Carried, unchanged: a lone voter's first election can wait for the 250 ms health sleep. | service.cc service loop | As in round 1. |
| F2 | no | defect | Carried, unchanged: a snapshot install is not counted as relay turn progress (up to 100 ms). | host.cc | As in round 1. |
| F3 | no | gap | Carried: no relay test runs host.cc's sleeping turn. | relay_service_fixture.hh | As in round 1. |
| F4 | no | gap | Carried: read test_authority_idle_wake (and now test_authority_restart_wake) in CI at the landing commit. CI at dbea4a53 is running, and macOS arm64 runs keep losing their runner. That predates 39e5ab7e (first loss at 8d56b21a), so it is not attributed to these commits. | CI | As before. |
| F6 | no | nit | Carried: read "no wakeups" from `interrupt_wakeups_per_s`. | idle_loops.py | As in round 1. |
| F7 | no | defect | Closed (round 3). | 3b65cd | None. |
| F8 | no | nit | Closed (round 3). | test_coordinator_teardown.cc | None. |
| F9 | no | gap | Closed (round 3). | 8e4934 | None. |

## Not run

- 261120-160911-3db6fb (the three-voter stop-time probe) was queued when I wrote this. Read its head
  against prefix-stop lines. It measures F13 and does not decide it.
- The suite at dbea4a53 (261120-144912-403ec3), A1 and A3 at bd01e34f, and CI at dbea4a53: all
  were running or queued.
- I did not audit the consensus Worker's initialization path beyond its budgeted limits. Q1's "no
  endless spin" rests on main having the same exit conditions.
- The arm64 runner losses: not investigated (the contributor's bisect is running).
