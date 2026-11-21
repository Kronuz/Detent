# multi-voter-idle-wake: journal

State: blocked on lead: no Peers wake exists in consensus (B1), and the consensus bump is a pin wave (B2)
Quest: .campaign/detent/quests/multi-voter-idle-wake.md (the lead's; never edit it)

## Doing

Nothing: both steps the quest needs are outside this quest's reach (Blocked on). Probe jobs at
Detent:eab1d2d9 (consensus a73c6e3 with the pins it declares): build 261121-131854-a90774, pins
261121-131855-852b66.

## Next

1. Lead: rule on B1 and B2 below.
2. After the rulings: service.cc drops `wake_by`; the turn sleeps until min(its deadlines,
   origin + `node.worker().next_deadline()` ms); `transient` loses `peers` and gains what Peers
   leaves the owner (a retained input admit() refused: `occupancy().retained_inputs`, a channel
   transfer in flight, `worker().runnable()`); the stop loop's `peers && !retiring` sleep goes too.
   Peers' wake raises `OwnerWake`. Tests in tests/detent_authority: A1 three voters idle 10 s
   counting `owner_turn_ns.count`; A2 a frame during an idle wait admitted within one turn, mutant
   dropping the wake.

## Blocked on

B1 (lead): consensus has no Peers wake at any commit, main a73c6e3 included. `Peers` takes only an
executor, a host and options; its reader retains a Control frame's input and waits on its own
`admission_wake` until the owner calls `admit()` (peers.h `reader`, `admit`), and calls nothing the
host could hook (`PeerHost` is `worker()` and `channel()`, read only in `admit()` and by the Bulk
lane). consensus:0acfaff's wake is the Driver's (driver.h: inputs mark a group ready), which the
authority does not use: it drives a Worker with `Peers`.
- (a) Recommended: a consensus quest (peers-owner-wake): `PeerOptions::wake`, a
  `std::function<void()>` (empty: none), called on the executor after each event that leaves the
  owner work: a reader retains a Control input, a Bulk frame reaches the channel, a session closes,
  a snapshot frame is consumed. Detent passes `OwnerWake::raise`. A Peers test counts one wake per
  received frame and none at rest. This quest then pins past it.
- (b) Detent wraps Peers' executor so every Peers completion raises the wake: no consensus change,
  but it wakes on every write, keepalive and timer too (not "only deadlines and frames"), and an
  `any_io_executor` adapter is fragile. Not recommended.
- (c) Fold into detent-on-drivers (Driver + HostTransport wake groups on input): no Peers change,
  but far later.

B2 (lead): the bump is a pin wave, not one pin. The first consensus main commit past 0acfaff
(consensus:8894776, 2026-11-20) and every later one pin journal 9e11713, completion d8b538c and
task 95798a0; Detent pins journal e5f6ff1 (97 commits behind), completion 560c288 and task
bdafa5f. The declared task pins disagree among the libraries: consensus and journal 9e11713
declare 95798a0, completion d8b538c declares 18eb516, http c813531 and reactor ab69cf2 declare
bdafa5f (their heads 18eb516). So check_pins (CI) fails whatever task Detent picks until those
libraries agree, which is pin-wave's work (msg-193 there), and cmake/dependencies.json is not in
this quest's file set (server-composition-adoption also changes it).
- (a) Recommended: order this quest after pin-wave (or a consensus-only wave that moves consensus,
  journal, completion and task together once their task pins agree) and after B1(a). This quest
  then changes only service.cc and its tests.
- (b) Carry the cascade here (Detent:eab1d2d9 is that probe): only if the build job passes, and
  with check_pins red until task pins agree, so it cannot land green.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |

Probe jobs (not evidence): 261121-131854-a90774 (build, consensus a73c6e3 cascade),
261121-131855-852b66 (check_pins at the same commit).

## Result

None yet.

## Rulings I made

- R1: the probe commit Detent:eab1d2d9 edits cmake/dependencies.json, outside the file set, because
  the assignment names the pin bump; it moves journal, completion and task with consensus because no
  consensus main commit past 0acfaff pins the journal Detent pins. Reverted if the lead rules B2(a).
