# consensus-quiesce-election-stall: journal

State: waiting on jobs 261121-141823-59e17c 261121-141825-00c318 261121-141828-d3de36 261121-141831-ab6b36 261121-141834-bf5253 261121-141837-c1601b
Quest: .campaign/detent/quests/consensus-quiesce-election-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code done at consensus:c45bb24 (pushed); evidence jobs queued. Cause (from a per-message trace of `driver_replace_test 1 1080 quiesce`, seed 1080 even: no witness; leaving member 1, removed; leader 4 term 2, followers 2 and 3):

1. Removed node 1 keeps pre-voting (term 3) every timeout. At t=9303 its pre-vote wakes leader 4, which heartbeats 2 and 3 (commit 28). Both heartbeats arrive while 2 and 3 still rest, so they change nothing.
2. At t=9305 node 1's pre-vote reaches 2 and 3 and wakes them: election timers run from 9305, and each re-acknowledges its log to 4.
3. Those acknowledgements answer none of 4's flights; 4 is already awake, its peers' `told` equals the commit, so `leader_at_rest()` holds and 4 rests without heartbeating them. State: leader 4 at rest (no timer), followers 2 and 3 awake with election timers armed and no heartbeat coming.
4. At t=9412 node 3 times out and pre-votes; node 2 (last contact 9305, past election_min) grants. Node 3 campaigns term 3 at step 3779 of 3799; 4 (leader, untransferred vote) and 2 (woken, contact fresh) ignore its votes, and its term-3 AppendResponse deposes 4 at 9473. The settle window closes at ~9480 mid-election: "no leader after settling".

So it is not a follower that fails to detect a silent leader: it is a leader that rests while a follower's timer runs, so failure detection fires against a live leader. Fix (core.h, leader's AppendResponse handler): an unprompted success (no matching flight) clears that peer's `told`, so the leader heartbeats it back to rest before resting again.

## Next

1. When the jobs finish: fill Evidence, set State to ready for review.
2. Lead: the ctest registration below (Blocked on), then CI.

## Blocked on

Lead (file set): the named test as a ctest entry needs CMakeLists.txt, outside this quest's files. Options: (a) add to CMakeLists.txt after the `consensus_driver_replace_${first}` loop:
`add_test(NAME consensus_driver_replace_rest_1080 COMMAND consensus_driver_replace_test 1 1080 quiesce)` plus a rest sweep, e.g. `foreach(first 1 251 501 751) add_test(NAME consensus_driver_replace_rest_${first} COMMAND consensus_driver_replace_test 250 ${first} quiesce) ...TIMEOUT 300`; (b) leave it as a job-run named case. Recommend (a): CI then runs it under the sanitizers on every push. Everything else proceeds without it.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261121-141823-59e17c (named seed 1080 at rest), 261121-141825-00c318 (1-3000 at rest, Release), 261121-141828-d3de36 (ASan+UBSan), 261121-141831-ab6b36 (TSan). Local run at c45bb24 before the oracle edit: 3000 of 3000 at rest ok, longest leaderless 329 ms; plain 1-1000 unchanged (3549 elections, identical counts with and without the fix) |
| A2 | 261121-141023-81b8cd: `--fails-without` at the pre-fix commit consensus:de24723, seed 1080 at rest exits 1 "no leader after settling"; 261121-141837-c1601b pending: the wake-ack-unanswered mutant (driver_mutants.py) at head |
| A3 | pending: 261121-141834-bf5253 (TSan ctest: simulation incl. quiescence cases, driver, driver_close*, liveness, driver_replace parts), 261121-141831-ab6b36 |
| A4 | consensus docs/mechanisms/quiescence.md, Rest, "A woken follower's acknowledgement is answered" |
| A5 | CI at consensus:c45bb24 running (pushed) |

Other jobs: 261121-134216-fb35b7 (failed, as intended): the reviewer's quiesce_trace.sh at consensus main 8780e44 reproduces seed 1080's "no leader after settling".

## Result

consensus:c45bb24 (on consensus:de24723). core.h: a leader's AppendResponse that answers none of its flights clears that peer's `told`, so `leader_at_rest()` waits for a heartbeat round to that follower. Docs: docs/mechanisms/quiescence.md (Rest, the woken-follower rule), CHANGELOG.md (own bullet, 2026-11-21 Fixed). Test: `driver_replace_test [seeds] [first] quiesce`; settle reports elections and the longest leaderless span and fails past 3 x election_max. Mutant: test/driver_mutants.py `wake-ack-unanswered`.

Open: the follower's wake acknowledgement uses its own `token()` as rpc, which could equal one of the leader's flight rpcs; a match with equal `through` would count as that flight's answer and set `told` again. Rare (both numbers must coincide); not changed here.

## Rulings I made

1. The cause is a leader resting over an awake follower, not a follower missing a silent leader: the fix is on the leader's side (answer the wake acknowledgement), the follower's rules are unchanged.
2. Any unprompted success clears `told` (also late or duplicated answers): it costs at most one extra heartbeat round, and tells a woken follower's acknowledgement from a stale one without a wire change.
3. A2's pre-fix commit is consensus:de24723 (the test mode, no fix); the persistent mutant is driver_mutants.py's `wake-ack-unanswered`, run at head.
4. Election bound in the oracle: 3 x election_max (600 ms) without a leader while settling; measured max 329 ms at rest, 236 ms plain.
