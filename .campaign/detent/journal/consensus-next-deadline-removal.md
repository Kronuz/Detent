# consensus-next-deadline-removal: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-next-deadline-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 2 (correctness review CHANGES at d9c7747) done at consensus:fcf7f66, pushed.
consensus:47ea920 = the plan-vs-Tick test with d9c7747's core (fails-without commit);
consensus:fcf7f66 = the fix. Local: core_deadline_test 41 checks, 0 failures at fcf7f66; at
47ea920 2 failures (plan named nothing yet the Tick appended; full-log check); membership,
liveness pass locally.

## Review findings (correctness, d9c7747)

- R1 (blocking): removal() restored to main's (no fit check); next_deadline() reports the removal
  only when its entry fits, after retirement. Class: every Core query a planner reads between its
  retire and the step: busy(), role(), removal(), capture_usage() (deadlines.md, "What a planner
  reads"); only removal() could differ; test removal_plan_matches_tick (fails at 47ea920).
- R2: quiet_before now requires the deadline ahead of the clock.
- R3: deadlines.md states the slice (256 entries and command_bytes) and the turn bound.
- R4: step() returns for a fenced Core before retiring.
- R5: run: probe r5.sh (scarce control pool, recovered follower's campaign Tick refused):
  10 Ticks refused with Pressure, next_deadline 0 after each, so Driver::arm() arms in the past
  every pass. Pre-existing on main (job 261124-141945-a3e432 at e0fd383). Fix is in driver.h,
  outside my file set: see Blocked on.

## Next

1. Paste the brief's table when the jobs finish; accept R1 against the suite job.

## Blocked on

Lead: R5's fix is in include/consensus/driver.h, outside the file set. Proposal: arm() takes the
last turn's Pressure into account, `if (pressured) { at = std::max(at, scheduler_.now() +
retry_ns); }`, so a refused due Tick retries after retry_ns instead of every wheel pass. Options:
(a) widen this quest to driver.h (plus a Driver test); (b) a follow-up quest (pre-existing on
main for a follower's campaign); recommend (b), since main has it and this quest only adds the
removal and hand-off to the class.

## msg (coordinator: file set widened)

Done: consensus:d9c7747 drops holdable() from test/liveness_test.cc (every Tick before
next_deadline() is held; LIVENESS_VERIFY also checks retirement) and its sentence in
docs/mechanisms/liveness.md. Core unchanged, so A1 4ce8df and bcb45d stand; A3 and the suite
requeued (d41f93, c31551); variant.py handles both harness shapes. Pushed.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-141928-74e621 (fcf7f66); fails-without 261124-132225-bcb45d (d31b547, main's core); R1's plan-vs-Tick fails-without 261124-141933-6502f0 (47ea920); earlier 4ce8df passed at e42c01c (stale) |
| A2 | docs/mechanisms/deadlines.md (Tick table, omissions, planner queries); tests in the A1 jobs |
| A3 | 261124-141938-899225 (fcf7f66); fails-without 261124-132235-b8ca66 (d31b547); a79939, d41f93 cancelled (stale) |
| A4 | suite 261124-141955-da2268 at fcf7f66 (campaign verify takes no --minutes); c31551 passed at d9c7747; `ci:ci` on the pushed fcf7f66 |

## Result

Core::next_deadline() now reports every Tick that acts (consensus docs/mechanisms/deadlines.md).
The table found four omissions, each with a by-value test in test/core_deadline_test.cc:
1. D3 removal (A1): due at once once removal() holds.
2. D9 witness hand-off: due at once when a live data voter exists (its answer came in a Receive).
3. D4 hand-off: the drive that starts the transfer no longer returns before its flights.
4. Retirement: compacted entries (charged to the log until retired, 256 per step) make a Tick due at once.
Also: removal() holds only when its entry fits the log (configuration_fits, shared with
propose_membership), so a full log cannot spin a driver; a non-voter's Tick no longer rewrites its
unread election deadline. Probes: .campaign/detent/probes/consensus-next-deadline-removal/
(a1.sh, held.sh, variant.py). The liveness harness now holds every Tick before next_deadline() (consensus:d9c7747).

## Rulings I made

- CMakeLists.txt: one token (core_deadline in the test foreach) to build the file set's own test.
- Retirement is in the class: the contract is "the earliest time a Tick changes Core's state", and a
  Tick retires charged entries; held-Tick traces need it (the probe's verify also checks it).
  Cost: after a compaction, about log_entries/512 + 2·log_bytes/command_bytes Worker turns at once (R3).
- Non-voter reset_election removed rather than reported: unobservable (every path to voter resets
  the timer: follower() on AppendRequest/Install, InstallActivated, Start), so maximum is exact.
- D4 fixed by dropping the early return (the first drive then matches every later one), not by
  reporting a past flight deadline.
- A3 shown with a probe variant (holdable() true for every node) instead of editing
  test/liveness_test.cc, which is outside the file set.
