# consensus-fetch-alongside-flake: journal

State: working
Quest: .campaign/detent/quests/consensus-fetch-alongside-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Diagnosis (A1). Instrumentation landed on the branch: consensus:3965055 (each inexact fetch's
error or first differing byte, commits past 2 s, the term change, per-node links / peers gone /
aborted calls, the process's worst stall) and consensus:aa3de7a (`LoopLags`: the longest each
loop went without turning; the first loop 1.5 s late prints every thread's stack).

Probes so far (local x86_64 Mac, build of the branch, scratch scripts in
~/.scratch/detent/consensus-fetch-alongside-flake, not evidence jobs):
- SIGSTOP of the whole process 3 s / 6 s mid-fetch: term lost, commit 3.1 / 6.3 s, but 16 of 16
  exact, no link lost (the late-deadline rule covers a whole-process stop). So a runner stopping
  the process does not give CI's 13 of 16.
- One loop blocked 6 s (a posted sleep, uncommitted hook block-hook.diff): node 2 loop 0 gives
  exactly CI's "13 of 16", each inexact one `connection_aborted` (dead_after 5 s ends node 2's
  links); node 1 (leader) loop 0 gives term lost + commit 5,080 ms + 15 of 16. No wrong bytes.
- Every node's connections and its group's Driver and journal (InlineCompletion, F_FULLFSYNC on
  macOS) are on loop 0; loop 1 idles (1-29 ms lag). Baseline loop 0 lag 55-77 ms.
Working hypothesis: something blocks one node's loop 0 for >= 5 s on the CI runner (a slow
synchronous journal sync is the candidate); the stack dump at 1.5 s will name it.

Jobs: 261123-133204-e7abf4 (30 runs serial, 4 burners), 261123-133207-5107e0 (24 runs, 3 at
once, 4 burners) at aa3de7a. CI run 45270266798 at aa3de7a (macos-x86_64 prints only on failure).

## Next

1. Read the jobs' and CI's loop lags and stacks; name what blocks the loop.
2. Fix by cause (test design if the journal's sync on the loop; library report if transport).

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
