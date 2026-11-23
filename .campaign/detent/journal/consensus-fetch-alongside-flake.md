# consensus-fetch-alongside-flake: journal

State: waiting on jobs 261123-141158-c6d68a 261123-141159-efa9e4 261123-141201-2e0693 261123-141202-3e37e0 261123-141204-1b7c5f
Quest: .campaign/detent/quests/consensus-fetch-alongside-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed (consensus:9011df3, merged with main at consensus:be2ffef, pushed); evidence jobs
queued. Commits on the branch:
- consensus:3965055 per inexact fetch its error or first differing byte; commits past 2 s; the
  term change; per node links, peers gone, aborted calls; the process's worst stall.
- consensus:aa3de7a `LoopLags`: each loop's longest wait to turn; the first loop 1.5 s late
  prints every thread's stack (print_stacks, factored from the watchdog).
- consensus:8429f45 `TimedInline`: the longest single journal operation per loop.
- consensus:16c5bfa `FETCH_ALONGSIDE_SLOW_SYNC=ms`: one follower's next sync that slow.
- consensus:9011df3 the fix (below).

Cause (local probes, Intel Mac, same arch as CI):
- Every peer connection of these nodes is on loop 0 (slot(1..3) -> loop 0 of 2) and the group
  was on loop 0 too; the journal runs inline (InlineCompletion; F_FULLFSYNC on macOS) on it.
- Loop 0's lag equals its journal hold: 70/83/60 ms lag vs 74/89/65 ms longest journal op.
- One loop blocked 6 s (posted sleep) on node 2 gives exactly CI's "13 of 16": node 2's links
  end at dead_after 5 s, all three fetches through them `connection_aborted`; on the leader it
  gives the term change and a 5,080 ms commit. Never wrong bytes.
- A whole-process SIGSTOP of 3 / 6 s gives the term change and slow commit but 16 of 16 exact:
  the late-deadline rule keeps links up when every thread stops. CI's 13 of 16 needs one loop
  held, and the journal's sync is the one blocking call on it.
CI's two failures (44996244171, 45240308075 attempt 1) both ran beside the install case
(ctest --parallel 2), which syncs heavily on the same runner disk.

Fix (test only, groups_test.cc): the group sits on loop 1, which serves no connection (asserted),
so a slow sync holds the Driver, never the links: every fetch must arrive exact, unconditionally.
The 2 s commit bound becomes 2,000 ms + the host's take (longest journal op + longest process
stall); a term change is allowed only once that reaches the 1,000 ms election timeout.

Jobs: A1 261123-141158-c6d68a (signature.sh at 16c5bfa: 6 s slow sync -> fails, inexact fetches
all errors, no wrong bytes, journal held >= 6 s); A2 261123-141159-efa9e4 (--fails-without at
16c5bfa, FETCH_ALONGSIDE_SLOW_SYNC=6000) and 261123-141201-2e0693 (same, with the fix); A3
261123-141202-3e37e0, 261123-141204-1b7c5f (25 runs each, 4 burners). Scripts:
~/.scratch/detent/consensus-fetch-alongside-flake/{repro,signature}.sh.

## Next

1. Read the jobs; A4 and A3's ci:macos-x86_64 from CI at be2ffef.
2. rpc-lane.md: no lane contract changed (the cause is the test's journal placement); leave it.

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
