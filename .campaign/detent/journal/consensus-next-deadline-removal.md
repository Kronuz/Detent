# consensus-next-deadline-removal: journal

State: waiting on jobs 261124-132220-4ce8df 261124-132225-bcb45d 261124-132230-a79939 261124-132235-b8ca66 261124-132249-c19e1f
Quest: .campaign/detent/quests/consensus-next-deadline-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 1 done at consensus:e42c01c (pushed). consensus:d31b547 = audit table + core_deadline_test
with main's core.h (the fails-without commit); consensus:e42c01c = the fix. Local (ASan): 
core_deadline_test 35 checks 0 failures at e42c01c, 8 failures at d31b547; D4 mutant (early return
restored) fails its one check; consensus, membership, pipeline, follower_read, core_deadline,
liveness, driver, quiescence, simulation, worker pass. A3 probe local: stepped == held == verify in
all 4 profiles, held Ticks per class (leaving/witness/nonvoter) default 2219/283/1835, FreeBSD
7466/1006/5522; at d31b547 it fails (19 "a held Tick changes nothing", FreeBSD TRACE differs).

## Next

1. When the jobs finish: paste the brief's table into Evidence; CI on e42c01c; State "ready for review".

## Blocked on

Lead (not blocking A1-A4): test/liveness_test.cc's holdable() guard (leaders with a leaving member
or unsettled membership, witness leaders, non-voters stepped every Tick) and docs/mechanisms/
liveness.md's "a D3 removal and a hand-off wait for a Tick, which next_deadline() does not report"
are now stale. Options: (a) widen this quest's file set to both and drop the guard (holdable()
becomes "always"; held.sh proves it equivalent, and the harness gets faster); (b) a follow-up
quest; (c) keep the guard. Recommend (a).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-132220-4ce8df (core_deadline_test at e42c01c); --fails-without 261124-132225-bcb45d (d31b547, main's core.h) |
| A2 | docs/mechanisms/deadlines.md (table, 4 omissions); each omission's test in the A1 jobs |
| A3 | 261124-132230-a79939 (held.sh 20 seeds at e42c01c); --fails-without 261124-132235-b8ca66 (d31b547) |
| A4 | suite 261124-132249-c19e1f; `ci:ci` on the pushed e42c01c |

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
(a1.sh, held.sh, variant.py). Leaves open: the liveness harness's holdable() guard and
docs/mechanisms/liveness.md's sentence about it are now stale (outside my file set; see Blocked on).

## Rulings I made

- CMakeLists.txt: one token (core_deadline in the test foreach) to build the file set's own test.
- Retirement is in the class: the contract is "the earliest time a Tick changes Core's state", and a
  Tick retires charged entries; held-Tick traces need it (the probe's verify also checks it).
  Cost: after a compaction a driver turns ceil(debt/256) times at once.
- Non-voter reset_election removed rather than reported: unobservable (every path to voter resets
  the timer: follower() on AppendRequest/Install, InstallActivated, Start), so maximum is exact.
- D4 fixed by dropping the early return (the first drive then matches every later one), not by
  reporting a past flight deadline.
- A3 shown with a probe variant (holdable() true for every node) instead of editing
  test/liveness_test.cc, which is outside the file set.
