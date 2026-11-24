# consensus-fetch-alongside-flake: journal

State: waiting on jobs 261123-194343-b857a5
Quest: .campaign/detent/quests/consensus-fetch-alongside-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix at consensus:4dbaee0 (pushed); every criterion has its jobs. Commits on the branch:
- consensus:3965055 per inexact fetch its error or first differing byte; commits past 2 s; the
  term change; per node links, peers gone, aborted calls; the process's worst stall.
- consensus:aa3de7a `LoopLags`: each loop's longest wait to turn; the first loop 1.5 s late
  prints every thread's stack (print_stacks, factored from the watchdog).
- consensus:8429f45 `TimedInline`: the longest single journal operation per loop.
- consensus:16c5bfa `FETCH_ALONGSIDE_SLOW_SYNC=ms`: one follower's next sync that slow.
- consensus:9011df3 the fix's bounds; its 2-loop placement was wrong (see below).
- consensus:4dbaee0 the placement: 4 loops, the group on the one no link uses.

Cause (local probes, Intel Mac, same arch as CI):
- On 2 loops, links to nodes 1 and 3 sit on loop 0 (slots 1385, 485) and links to node 2 on
  loop 1 (slot 3517); the group was on loop 0, where its journal runs inline (InlineCompletion;
  F_FULLFSYNC on macOS). A slow sync on node 2 held its links to nodes 1 and 3.
- Loop 0's lag equals its journal hold: 70/83/60 ms lag vs 74/89/65 ms longest journal op.
- One loop blocked 6 s (posted sleep) on node 2 gives exactly CI's "13 of 16": node 2's links
  end at dead_after 5 s, all three fetches through them `connection_aborted`; on the leader it
  gives the term change and a 5,080 ms commit. Never wrong bytes.
- A whole-process SIGSTOP of 3 / 6 s gives the term change and slow commit but 16 of 16 exact:
  the late-deadline rule keeps links up when every thread stops. CI's 13 of 16 needs one loop
  held, and the journal's sync is the one blocking call on it.
CI's two failures (44996244171, 45240308075 attempt 1) both ran beside the install case
(ctest --parallel 2), which syncs heavily on the same runner disk.

Local probe at 16c5bfa with FETCH_ALONGSIDE_SLOW_SYNC=6000 (not a job): node 2's journal held
loop 0 6,042 ms, its links ended at 5,047 ms, its three round-1 fetches "Software caused
connection abort", 13 of 16 exact, commits past 2 s; identical to CI. The fixed build compiles.

Not yet proven on CI itself: that the runner's blocker was F_FULLFSYNC. The match of signature
and the journal being the only blocking call on that loop are inference; any further CI failure
now prints the journal's hold and the blocked loop's stack.

Fix (test only, groups_test.cc): 4 loops, links on 1, 3 and 0, the group on loop 2, which serves
no connection (computed, asserted, printed), so a slow sync holds the Driver, never the links:
every fetch must arrive exact, unconditionally.
The 2 s commit bound becomes 2,000 ms + the host's take (longest journal op + longest process
stall); a term change is allowed only once that reaches the 1,000 ms election timeout.

Scripts: ~/.scratch/detent/consensus-fetch-alongside-flake/{repro,signature}.sh.

Second contributor, 2026-11-23 (the first's context filled):
- The fix's placement assertion failed on every platform (CI be2ffef: the only failure on all 9
  jobs; job 2e0693), not one build type: node ids 1, 2, 3 have slots 1385, 3517, 485, so on 2
  loops node 2's links sit on loop 1 and nodes 1 and 3's on loop 0. No loop of 2 was free; the
  "slot(1..3) -> loop 0" premise was wrong (links to node 2 were on loop 1 all along).
- consensus:4dbaee0: 4 loops (links on 1, 3, 0), the group on the loop no node's links use
  (computed, asserted, printed); LoopLags and the per-node line take the loop count ("a/b/c/d").
  Local, Release: plain run 16 of 16 exact; FETCH_ALONGSIDE_SLOW_SYNC=6000 on node 2 holds only
  loop 2 (6,048 ms without turning, journal 6,053 ms), 0 peers gone, 16 of 16 exact, commit
  6,121 ms within its 8,088 ms bound, leader kept term 1.
- A1/A3 jobs (c6d68a, 3e37e0, 1b7c5f) failed on the scripts, not the case: both took the build
  path relative and cd'd into it first, so `$build/...` paths broke. They now resolve it
  absolute (checked from the worktree root: repro 2 of 2 pass, signature runs). repro.sh greps
  the new per-loop format.
- Merged origin/main (consensus:3f99bea) before the fix.

## Next

1. Read A1 job 261123-194343-b857a5 (at head, the pre-fix code via --ref PRE=16c5bfa); when it
   passes, State "ready for review".
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
| A1 | 261123-194343-b857a5 (queued; at consensus:4dbaee0, signature.sh on the pre-fix build via --ref PRE=16c5bfa). Same command at 16c5bfa passed: 261123-164648-869a55 (13 of 16 exact, all 3 inexact fetches errors, none wrong bytes, sync held 6,031 ms; "ran other code" for the gate). Failed on the script's relative path: 261123-141158-c6d68a. Cancelled repro runs: 923832, 4475c9, 72f031, d47a61, 366894, dbf1fa, 262869 |
| A2 | 261123-164650-bbaf94; fails without: 261123-141159-efa9e4. Failed: 261123-141201-2e0693 (the 2-loop placement assertion) |
| A3 | 261123-164653-3cb87a, 261123-164658-9dea3a (0 of 25 failed each, 4 burners); ci:macos-x86_64 green in CI run 45299903223 at consensus:4dbaee0 (attempt 2). Failed on the script's path: 261123-141202-3e37e0, 261123-141204-1b7c5f |
| A4 | CI run 45299903223 at consensus:4dbaee0 green, all jobs (attempt 2); the suite check (ci:macos-x86_64) green in it. Attempt 1's macos-x86_64 failed in consensus_transport_rpc_host_stream ("heartbeats' longest gap 1170 ms"): outside this diff, filed by the lead as quest consensus-rpc-host-stream-gap. CI at be2ffef (run 45277444899) failed on all 9 jobs on the placement assertion alone, fixed by 4dbaee0. At the landed commit: the landing gate's (main moved 2 unrelated files, c177c88, 5b1ffb6) |

## Result

Cause: a test placement, not a transport defect. The case's journal runs inline on its group's
loop (InlineCompletion), and that loop also served node 2's links to nodes 1 and 3. A runner
sync of 5 s or more ended those links at dead_after: 3 fetches `connection_aborted` (CI's "13 of
16 exact"), and commits took 4.4-7.1 s. No fetch ever returned wrong bytes. Injecting a 6 s sync
reproduces it: 869a55, 13 of 16 exact, the sync held 6,031 ms, every inexact fetch an error.

Fix (test/transport/groups_test.cc only, consensus:4dbaee0): 4 loops, the group on the one loop
no node's links use. The commit bound is 2,000 ms plus the host's take, and a term change is
allowed only past the 1,000 ms election timeout. With the same 6 s sync: 16 of 16 exact, 0 peers
gone, commit 6,111 ms within 8,038 ms, term kept (bbaf94). 50 of 50 runs pass beside 4 burners
(3cb87a, 9dea3a). Any later failure prints each inexact fetch, every loop's lag and journal hold,
and the blocked loop's stack. No contract changed; docs/mechanisms/rpc-lane.md is untouched.

For the correctness review: can a group share a loop with its peers' connections in production,
and does a slow sync then stall heartbeats the same way?
- Sharing: yes, by design. Groups and links place by the same hash over the same 4,096 slots:
  `loop_of_group` and `loop_of_node` (consensus include/consensus/transport/host_transport.h:424-429)
  both call `host_detail::loop(host_detail::slot(id), loops())` (journal include/journal/host.h:92-97),
  and `links->owner` hands each node's links to `loops_[loop_of_node(node)]` (host_transport.h:412).
  On 1 loop everything shares. On L loops a group shares with the links to any peer whose id lands
  in its loop's slot range.
- The stall: only when the embedder runs the journal on InlineCompletion, as the blocking entry
  (D5, docs/DRIVER.md:153-155) and this test do. Then a slow sync blocks every connection on that
  loop, heartbeats included, exactly as here. With the native queue (completion
  include/completion/native_completion.h:40, `make_native_queue`), the loop only submits and polls.
  On macOS, reads and writes go to AIO and syncs to the queue's fallback worker thread
  (bsd_completion.h:105-133). On FreeBSD, aio_fsync is used. Linux uses io_uring or its fallback
  worker (linux_completion.h:72). In those cases a slow sync delays that group's own turn, not its
  loop. Consensus builds no queue itself (docs/GOAL5-AUDIT.md:34), so the embedder chooses.
- Open, outside this quest: an embedder that pairs InlineCompletion with HostTransport on a slow
  disk gets this same failure. That hazard belongs in the transport's or host's docs, the lead's
  call.

## Rulings I made

- The cause is the test's placement, not the lane: no contract changed, rpc-lane.md untouched.
- The group moves to the loop with no connection rather than loosening fetch exactness: a
  fetch must still arrive exact under any host, so wrong bytes or a lane starvation still fail.
- Commit and term bounds take the host's measured take (journal op + process stall), as
  consensus:2d42387 did for heartbeats; the term only past the 1,000 ms election timeout.
- FETCH_ALONGSIDE_SLOW_SYNC stays in the case as the reproducible injection for A2.
- Local experiment hook (posted loop sleep) kept out of the repo: block-hook.diff in scratch.
