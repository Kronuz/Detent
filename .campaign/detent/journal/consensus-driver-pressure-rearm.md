# consensus-driver-pressure-rearm: journal

State: working
Quest: .campaign/detent/quests/consensus-driver-pressure-rearm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Table written (below); building a local driver_test to build the Pressure fixture. Rule decided: a step that
ends on a Pressure turn floors its wheel entry at now + retry_ns (the Driver's existing refused-input
retry), never at Core's past deadline. No new timer, no new state.

## Next

1. Fixture: a recovered lone voter (follower, election deadline due) on a scarce control pool; Pressure held, then cleared by a checkpoint's reclaim.
2. Fix in Driver::step/arm; test/driver_pressure_test.cc counting wheel arms by value; docs/mechanisms/deadlines.md rule.
3. At the head: A1 + fails-without, A2, suite (45 min); push.

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

Rule: Driver::step records whether the turn that ended it was Pressure; arm() then raises the Core
deadline to `now + retry_ns` when it is not ahead of that, and lowers a later one to it. The floor is the
existing `retry_ns`; there is no new timer, state per group is one bool.
(The only external wake would be a hook on the pool's release; journal's Admission has none, and none
is needed: every release is a step of the group.)

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

## Rulings I made

1. Floor is the existing retry_ns (1 ms), not an escalating back-off: one rule shared with refused inputs (`waiting`).
