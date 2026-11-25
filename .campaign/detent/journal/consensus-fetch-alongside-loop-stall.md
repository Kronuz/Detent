# consensus-fetch-alongside-loop-stall: journal

State: waiting on jobs 261124-212647-bda88f 261124-212704-2b98ad 261124-212720-d82038 261124-212804-eb47f1 261124-212808-829111 261124-212812-9fd35e 261124-212822-225726; CI 45553828878
Quest: .campaign/detent/quests/consensus-fetch-alongside-loop-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix done and pushed (consensus:ee78a29 = main be18ad9 merged; CI 45553828878). Commits:
- consensus:10747e0 FETCH_ALONGSIDE_SLOW_READ=ms (node 2's first two replies that much slower
  to build); LoopLags' 1.5 s dump reads each loop's run state and CPU before and after the
  stacks (thread_cpu_us, shared with LoopProbe).
- consensus:1af7530 the fix: each round's replies built on the main thread before its fetches
  go out; the reader moves one out; a fetch asked again past them builds on the loop (counted).

Cause (CI 45372452058, log Detent/.scratch/detent/ci-pipesim/run-45372452058.log):
- Work on node 2's loops 0 and 1: serving node 3's and node 1's fetches (links to 3 on loop 0,
  to 1 on loop 1). The 1.5 s dump's `sample` has threads 64396, 64397 (node 2 loops 0, 1) and
  64407 (node 3 loop 3, serving node 2) in serve_fetch -> the case's reader, at
  std::function::operator() +152/+174/+201. A local build with CI's flags puts exactly those
  offsets in pattern()'s byte loop, after its __bzero: the threads were executing the loop
  (PCs move between samples), not faulting in bzero, not in a syscall. Every other loop is in
  kevent or a condvar; no TLS, connect, DNS or lock frame.
- Why seconds: that loop is 13-22 ms of CPU here (.scratch probe pat.cc); on the runner it ran
  ~7 s while idle loops turned within 14 ms before the dump, and `sample` started ~5 s late and
  got 2 samples in 1 s. So the runner ran CPU-bound threads ~400x slower than wake-ups: a
  handler that computes for 15 ms is a 7 s loop hold there. Not a kernel wait inside our code.
  The new dump line (state + CPU across the stacks) confirms R-without-CPU vs U next time.
- Then: node 2's links end at dead_after 5 s (4 peers gone), its fetches connection_aborted,
  commits to node 2 wait (7,150 ms), node 2's election takes node 1's term.
- Main's heartbeat-gap failures (45366654906, 45374547236 job 138381843363, 45425539843 job
  138556593380) are rpc_host `fetch`: gaps 1564/3746/3765 ms with host lag 10-22 ms. Its reader
  copies 16 MiB per call (`return expected;`) on the serving link's loop: the same shape (one
  bulk handler on a connection loop, ~4 ms here), stretched alike. rpc_host_test.cc is outside
  this file set: for the lead (Blocked on).

Local (x86_64 i9, CI flags RelWithDebInfo -O2 -g extensive hardening), not jobs:
- main plain x3: node 2 loops 0/1 13-18 ms, node 3 loops 1/3 10-15 ms (serving), node 1's
  loops 1 ms; journal 0 on all of those; process stall 5 ms.
- 10747e0 SLOW_READ=6000: node 2 loops 6020/6003 ms, journal 0/0, stall 30 ms; links 6/7/5,
  4/4/0 peers gone, connection_aborted 1/3/2: CI's counts exactly; commit 6,184 ms; fails.
  (16 of 16 exact now, CI 13 of 16: main's asked-again from fetch-oversize-abort.)
- 1af7530 SLOW_READ=6000: passes, serving loops 1-2 ms, 0 peers gone; plain: 1-4 ms.

Scripts: ~/.scratch/detent/consensus-fetch-alongside-loop-stall/{signature,lags,repro}.sh
(repro.sh copied from the flake quest).

## Next

1. Read the jobs and CI 45553828878; fill Evidence; State "ready for review".

## Blocked on

Nothing for this quest. For the lead (outside the file set, not done):
- rpc_host `fetch` heartbeat gaps (main, 3 runs above): its reader copies 16 MiB per call on
  the serving loop. Options: (a) the same test fix in rpc_host_test.cc (move a pre-built reply
  out); (b) FetchReader returns shared bytes (std::shared_ptr<const std::string>) so no host
  copies a reply on a connection loop: a contract change of include/consensus/transport/fetch.h
  and the D8 docs. Recommend (a) now as its own quest, and (b) only if production readers
  serve large files from memory (peer-fetch.md already says to serve from memory).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261124-212647-bda88f (signature.sh at consensus:10747e0, SLOW_READ=6000: node 2's loops 0/1 held >= 6 s, journal 0/0, stall < 1 s, links gone, 2 threads in serve_fetch's reader). Measurement: lags at main 261124-212720-d82038, at the fix 261124-212804-eb47f1. Cause: Doing |
| A2 | 261124-212808-829111 (SLOW_READ=6000 passes at ee78a29); fails without: 261124-212704-2b98ad (same at 10747e0). Bounds unchanged: 1af7530 touches no check |
| A3 | 261124-212812-9fd35e (25 runs beside 4 burners, strict, at ee78a29) |
| A4 | suite 261124-212822-225726 (campaign verify at ee78a29); CI 45553828878 |

Superseded, not failures: 261124-212651-a6ec88, 261124-212655-c6962c, 261124-212659-d0b200,
261124-212724-a26bae (queued at 1af7530 or replaced before the merge).

## Result

Cause: the case's fetch reader built each 8 MiB reply (a byte loop, ~15 ms here) on the loop
serving the fetching node, which also carries that node's Control link; CI's macos-x86_64
runner ran it ~7 s, so node 2's links ended at dead_after and commits stalled. Fix
(test/transport/groups_test.cc only, consensus:1af7530): each round's replies are built before
its fetches go out and the reader moves one out, as docs/mechanisms/peer-fetch.md asks of a
host. Serving loops' longest turn: 10-18 ms at main, 1-4 ms at the fix (lags jobs); with the
reply 6 s slow to build the fix passes and main fails. Every check and bound unchanged. A CI
failure now prints each loop's run state and CPU around the stacks. Open: rpc_host `fetch`'s
16 MiB copy per reader call (Blocked on).

## Rulings I made

- The fix is in the test, not the transport: the transport's contract runs the reader on the
  connection's loop and peer-fetch.md tells hosts to serve from memory; this case's reader
  computed its reply there. Moving the reader off the loop would change the reader's thread
  contract (the lead's).
- The reproduction injects time into building node 2's replies, wherever they are built: the CI
  sample put the held loops inside that build, so the knob slows the work CI saw slow. At the
  fix the build runs on the main thread, between commands; no commit's time counts it.
- Replies are built per round (4 x 8 MiB at a time), not all 16 up front (128 MiB).
