# consensus-close-fenced review: correctness

Commit: consensus:50b91ae8dc8da2fdb7b1c99ebf0163418ebd8168
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The close and erase work is sound, and every finding from the round at c5d15d9 is closed. The A6
change fixes the spin but adds a hang. After shutdown, watch() is uncancellable. If a group stages a
record after the turn driver's run() has returned, nothing will ever land that barrier, so the
Driver's run() never ends. The old code ended once the other IO landed (probe e392f8).

## Evidence I relied on

- 261118-063956-fff668 (plain, 62 checks, 0 failures), 261118-064000-5e01ed (ASan+UBSan) and
  261118-064002-13f33c (TSan), all at 50b91ae: A1-A4 and A6's suite case pass.
- 261118-064120-75b4b1 (A6 mutant, watch back on `shutdown_.token()`): reached the test and failed
  exactly "the loop idles after shutdown with the barrier pending". So it targets A6's claim.
- 261118-064122-6a437c (N3 mutant, `if (g.queued)` becomes `if (false)`): heap-use-after-free in
  `pump()` at driver.h:607. erase_queued now reaches the queued branch, so N3 is closed.
- 261118-064218-d255a1 and 261118-064220-b8cb43 (Xapiand repro, with and without the fix): I took
  them as the journal reports them and did not reread their per-seed lines.
- Suite 261118-063950-78323a fails only consensus_transport_idle. The lead says that is
  pre-existing (transport-idle-budget), and I ignored it as asked.

## What I checked myself

Scripts are in `.scratch/review-consensus-close-fenced-correctness/` in the home checkout.
`probe_a6.sh` builds `probe_a6.cc`, which includes driver_close_test.cc's Loop. It builds twice
under ASan+UBSan Debug: once at the commit, and once with the watch token reverted to
`shutdown_.token()`.

- 261118-073914-e55bdd, first version (two cases):
  1. `fenced_close_shutdown`: a fenced row with a staged proposal, then close, then a one-token
     shutdown mid-close. The close completes with `task::cancelled` and the row stays until
     `~Driver` (groups=1), as the DRIVER.md "Shutdown order" says. Both runs end. The proposal
     failed `fenced` before placement. After a Host reopen, recover succeeds with committed=3.
     No placed record is lost.
  2. `hang`, first version: a two-voter follower's timer sends only a pre-vote and stages nothing,
     so the case did not reach the hang.
- 261118-075509-e392f8, `hang` (a higher-term `VoteRequest` from peer 2 delivered after the turn
  driver returned): see F1.
- I read driver.h's loop, step, watch, launch, shutdown and close_step, and journal
  turn_driver.h `run`, `durable` and `~TurnDriver` at journal:4272229. On Q1 and Q2:
  1. A stuck disk is not new. The turn driver's own run() waits on that barrier IO either way, and
     D8 already says run() returns once IO lands.
  2. Lifetime: `~TurnDriver` nulls each waiter's owner and never resumes it. So destroying a Driver
     whose run() never ended leaks the watch frame. It does not use freed memory, and the probe
     destroyed it under ASan cleanly. But the watch's callback captures `this` and `&g`
     (driver.h:944). If that Host's turn driver runs again after the Driver is destroyed, the
     barrier lands and the callback uses freed memory. `~Driver` does not check `in_flight_`.
  3. Close (Q2): the A6 change only affects watches started after `stopping_`, and close after
     `stopping_` fails at once (driver.h:342). Close on a fenced, half-opened or unknown row is
     unchanged from the last round (fff668). close_step still waits for `drained()`, `io` and
     `watching` before it sets `closed`, so a staged record lands before the row goes
     (close_in_flight).

## Prior findings (round at c5d15d9)

| ID | Status |
|---|---|
| F1 | Closed. docs/DRIVER.md:83, :91 and :135 now describe the erased row, `dropped(id)` for an id that is not open, and the Host's create and recover refusal. |
| F2 | Closed by the lead's ruling. A3 is rescoped to the Driver's rows. `created_bytes` (96c6ec) reports the Driver part as 0 and the journal part, owned by journal-reopen-closed. |
| N1 | Closed. The DRIVER.md close row states that the Host refuses create or recover until it reopens. |
| N3 | Closed (6a437c, above). |
| N4 | Half closed. The comment is fixed. `fenced_in_flight` still accepts a placement, `closed` or `fenced` (test/driver_close_test.cc:246). |
| N5 | Closed. tools/mutants.sh is no longer in the diff. |

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | Shutdown can now hang forever. After `stopping_`, `watch()` passes an empty token (driver.h:944), so `run()` returns only when that barrier lands. Steps go on after shutdown while other IO holds the loop, and plain `receive` still delivers. A step can therefore stage a record after the turn driver's `run()` has seen stop and returned ("returns once `stop` is requested and nothing is staged", journal turn_driver.h `run`). Nothing will ever commit that turn. The setup: one stop token for the turn driver and the Driver, as in Xapiand and shutdown_idles, and a two-voter follower that receives a higher-term `VoteRequest` after stop while another group's checkpoint IO is held. At 50b91ae the Driver's run never ends (in_flight=1 after every held IO lands). With the old token it ends (driver_done=1). | 261118-075509-e392f8: head `RESULT driver_done=0 turns_done=1 in_flight=1 held=0`; old token `RESULT driver_done=1 ... in_flight=0` | Options, the lead's pick: (a) keep the watch cancellable, and after `stopping_` never re-watch. A watch cancelled at or after shutdown does not mark the group ready to watch again. The turn driver still writes what is staged while it runs (D8), and run() ends when the other IO lands. This fixes both the spin and the hang, and I recommend it. (b) Stop staging after `stopping_`: drop `receive` and timer steps. This narrows behavior, so it is the owner's call. (c) Make it a contract: the turn driver runs until the Driver's run() returns. Xapiand's one-token shutdown breaks that today. The test: the probe's `hang` case in driver_close_test, asserting that run() ends once the other IO lands. Keep shutdown_idles, which must still idle. |
| N1 | no | gap | shutdown_idles checks in-flight counts and done flags, not values. It never checks that g's staged record is durable after shutdown ("a staged record is still written") or how `proposal` and `checkpoint` complete. | test/driver_close_test.cc:342-363 | After `release()`, check the TurnDriver's durable sequence for g, and that the proposal completed with `task::cancelled`. |
| N2 | no | gap | `~Driver` destroys a Driver that still has a watch or IO in flight without a word. Each in-flight callback captures `this` and `&g`, and F1 makes that state reachable. | driver.h:166-168, :944 | A Debug `assert(!in_flight_)` in `~Driver`, or a DRIVER.md line saying the Driver outlives its run(). |
| N3 | no | nit | The quest's A3 names "journal-reopen-closed A7". The journal and the lead's ruling say A4b. | quest file A3; journal "Blocked on" | The lead aligns the quest text. |

## Not run

- Did not rerun the Xapiand simulation or read d255a1 and b8cb43 past the journal's summary.
- Did not run TSan, the suite or CI. I accepted 13f33c, 78323a and the lead's CI note.
- Did not probe the blocking entry `consensus::run`. It commits turns inline until its loop
  finishes (driver.h:1027-1035), so F1 should not arise there, but no probe shows it.
- Did not check whether Xapiand's transport delivers `receive` after its stop token. F1 also
  arises from any other step that stages a record after the turn driver returns.
