# consensus-driver-pressure-rearm: journal

State: ready for review (jobs queued at consensus:c724c58; pushed)
Quest: .campaign/detent/quests/consensus-driver-pressure-rearm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review N1 (lead's ruling), N2, N3 folded in at consensus:c724c58; jobs queued, CI on the push.

## Next

1. Read the jobs and CI; land.

## Table: how a Tick is refused, and what wakes the Driver

Worker::run_one ends each turn with a TurnResult; the Driver loops while a turn makes progress and then
`arm()`s the group's one wheel entry at `Worker::next_deadline()`. A Pressure turn makes no progress, and
refuses without moving Core's clock, so the deadline stays at or before the clock: arm() files the entry
in the past and the next wheel pass re-steps the group (main: one step per pass, r5.sh job 261124-141945-a3e432).

| Refusal (run_one result) | Cause | Core deadline after | What wakes the Driver next | Today | Rule |
|---|---|---|---|---|---|
| Pressure from try_submit(Tick) (worker.h:1157 reserve_append, Control class) | a follower's campaign pack, or a leader's D3 removal append, does not fit the control pool or its ticket cap | unchanged, at or before the clock (r5: 0) | the group's own pool release: the pool is per group (journal Admission, one per Store), so every release (a permit refunded or consumed, a durable reclaim in `maintenance()`, an IO completion) is a step of this group that retries the Tick in the same step; nothing outside the group releases it | re-armed at the past deadline: one step per wheel pass until it clears | floor at now + retry_ns |
| Pressure from the maintenance funding (`capture_funding_pressure_`, worker.h:539/552/581/591) | a checkpoint's capture cannot be funded | Core's, unchanged (usually later) | the same releases | re-armed at Core's deadline (usually not due): no spin, but a later retry than the pool allows only if no event comes | the same floor: retry in retry_ns, Core's later deadline kept when it is sooner |
| Pressure from initialization (worker.h:517, `initialization_` append) | the first control append cannot be funded; the group is not started | none (not started) | its own releases | no wake until an event | the same floor (the Pressure turn arms retry_ns) |
| Pressure from admit(): inbox Receive, notices, calls, Start's Tick | refused input kept in the inbox | any | none outside | `waiting` branch: retry_ns, but past Core deadline still wins (min) | unchanged here: min() of a due deadline, which the floor now also covers when the last turn was Pressure |
| Busy (output, completion, persisting, maintenance due, timer_blocked) | Core or Worker occupied | `maximum` while persisting, else stale | the occupant's own completion (IO done, take_actions, applied) steps the group | unchanged | none: not Pressure |
| Fenced | terminal | `maximum` | none | disarmed | none |

Rule (docs/mechanisms/deadlines.md, "What the Driver re-arms"): Group::pressured is the last turn's
Pressure; arm() then files the entry retry_ns ahead, or at Core's deadline when that is ahead and
sooner, never at a deadline behind the clock. The floor is the existing `retry_ns`; no new timer, one
bool per group. A pool is one group's (journal Admission is per Store), so every release is a step of
the group, which retries the Tick in the same step; the retry covers a release from outside a step.
The same rule polls an unstarted group whose pre-Start Tick is pressured (before: no wake at all,
Core's deadline is `maximum` before Start); not tested, since a Worker cannot be pressured before its
create finishes.

## Blocked on

Nothing.

## Evidence

Final head consensus:c724c58. Local Debug: all four cases, 25 checks, 0 failures.

| Criterion | Evidence |
|---|---|
| A1 | hold 261124-180700-10aa9c; fails-without 261124-180734-8b1fb2 (main's driver.h, mutant3). N1 pair: ahead 261124-180709-310fda, rest 261124-180713-57d06f; fails-without at e61ac8f's rule 261124-180717-874feb (ahead), 261124-180721-a113e5 (rest). |
| A2 | clears 261124-180704-d1df01 |
| A3 | [artifact] docs/mechanisms/deadlines.md, "What the Driver re-arms" |
| A4 | suite 261124-180739-63d36e; CI on the push |

Superseded and cancelled by the new head: 261124-172806-e68a0a, 261124-172810-68a7c2, 261124-172812-a595f1 (passed at e61ac8f), suite 261124-172820-a9d732.

Review findings: N1 narrowed (`g.pressured && at <= now` only; ahead/maximum armed as they stand), tests ahead and rest. N2 doc reworded ("a turn the Worker ends with Pressure", "followed by a step"). N3 `pressured` cleared in the fence catch. N4 kept.

## Result

Driver::arm() re-arms a group whose last turn was Pressure and whose deadline is behind the clock retry_ns (1 ms) ahead instead of at that past clock; a deadline ahead or `maximum` is armed as it stands: 1 step per retry_ns, not per wheel pass (100 ms: 100 steps against 1000, none in the past). Core,
Worker and next_deadline() are unchanged. Files: include/consensus/driver.h, test/driver_pressure_test.cc
(cases hold, clears; ctest consensus_driver_pressure_hold/clears), CMakeLists.txt, docs/mechanisms/deadlines.md.

Left open for the lead:
1. A pressured Worker is nearly unreachable through the Driver: in shared (Host) mode `used` counts artifacts only, and a created pool is never clawed back, so Tick Pressure needs outstanding permits (an install's cutover permit) or a recovery census over the pool. The test holds four Control tickets through `-fno-access-control` (test TU only) and reaches Driver::groups_ and Worker::store_.
2. At recovery with `used` above `hard - control_pool`, Driver::admit's pre-Start Tick is pressured and the group never starts (unchanged; sleeps, no poll).

## Rulings I made

1. Floor is the existing retry_ns (1 ms), not an escalating back-off: one rule with refused inputs (`waiting`), and A2 wants the retry within one wheel pass.
2. (Superseded by review N1) only a deadline behind the clock gets retry_ns.
3. The test uses `-fno-access-control` on its own source to hold Control permits on the Worker's Store: no other way to hold a pool for an interval and release it through the Driver's API.
4. The fails-without commit f5069e0 (local branch scratch/pressure-rearm-mutant, never pushed) is the head with main's driver.h.
