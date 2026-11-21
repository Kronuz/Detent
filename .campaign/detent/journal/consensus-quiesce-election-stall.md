# consensus-quiesce-election-stall: journal

State: working
Quest: .campaign/detent/quests/consensus-quiesce-election-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 2: fix committed locally, sweeping. Cause (from a per-message trace of `driver_replace_test 1 1080 quiesce`, seed 1080 even: no witness; leaving member 1, removed; leader 4 term 2, followers 2 and 3):

1. Removed node 1 keeps pre-voting (term 3) every timeout. At t=9303 its pre-vote wakes leader 4, which heartbeats 2 and 3 (commit 28). Both heartbeats arrive while 2 and 3 still rest, so they change nothing.
2. At t=9305 node 1's pre-vote reaches 2 and 3 and wakes them: election timers run from 9305, and each re-acknowledges its log to 4.
3. Those acknowledgements answer none of 4's flights; 4 is already awake, its peers' `told` equals the commit, so `leader_at_rest()` holds and 4 rests without heartbeating them. State: leader 4 at rest (no timer), followers 2 and 3 awake with election timers armed and no heartbeat coming.
4. At t=9412 node 3 times out and pre-votes; node 2 (last contact 9305, past election_min) grants. Node 3 campaigns term 3 at step 3779 of 3799; 4 (leader, untransferred vote) and 2 (woken, contact fresh) ignore its votes, and its term-3 AppendResponse deposes 4 at 9473. The settle window closes at ~9480 mid-election: "no leader after settling".

So it is not a follower that fails to detect a silent leader: it is a leader that rests while a follower's timer runs, so failure detection fires against a live leader. Fix (core.h, leader's AppendResponse handler): an unprompted success (no matching flight) clears that peer's `told`, so the leader heartbeats it back to rest before resting again.

## Blocked on

Nothing

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
