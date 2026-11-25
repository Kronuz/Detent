# consensus-fetch-alongside-loop-stall: journal

State: ready for review (jobs queued: 261124-212647-bda88f 261124-212704-2b98ad 261124-212720-d82038 261124-212804-eb47f1 261124-215634-5e9c39 261124-215638-369ffc 261124-215644-81f10e 261124-215646-b613dc 261124-215649-56b54a 261124-215714-ba0954 261124-215716-07805b 261124-215719-47582c 261124-215720-0be582 261124-215723-0d3fbd; CI 45557132080)
Quest: .campaign/detent/quests/consensus-fetch-alongside-loop-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Both fixes done and pushed (consensus:4fa72a5; CI 45557132080). Commits:
- consensus:10747e0 FETCH_ALONGSIDE_SLOW_READ=ms (node 2's first two replies that much slower
  to build); LoopLags' 1.5 s dump reads each loop's run state and CPU before and after the
  stacks (thread_cpu_us, shared with LoopProbe).
- consensus:1af7530 the fix: each round's replies built on the main thread before its
  fetches go out; the reader moves one out; a fetch asked again past them builds on the loop (counted).
- consensus:ee78a29 main be18ad9 merged.
- consensus:ee77b26 rpc_host fetch: RPC_HOST_FETCH_SLOW_READ=ms (the 16 MiB reply that much
  slower to build, wherever built); prints the build's ms, replies built on host 2's loop, and
  each link loop's longest turn until the reply arrived (Turns: a probe posted every 5 ms).
- consensus:4fa72a5 rpc_host fix, the same shape: replies (big, huge, 5 slow) built before the
  loops start, the reader moves one out, a name past them builds on the loop (counted).

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

rpc_host `fetch` (main's failures: 45366654906 job 138350084469, 45374547236 job 138381843363,
45425539843 job 138556593380; logs in ~/.scratch/detent/consensus-fetch-alongside-loop-stall):
- CI: gaps 1564/3746/3765 ms of ~1010-1022 allowed, host lag 10-22 ms; links 2+2; reply exact
  in 4078/5386/4998 ms; dropped 385/672/632; five 4 MiB: 3/1/1 ok.
- Path: host 2's reader runs on loop_of_node(1), the loop holding both links to host 1; the
  heartbeat crosses that loop to Control. The reader copied 16 MiB there (`return expected`).
- Local (CI flags), knob on the reader at ee77b26: 1500 ms -> gap 1537, dropped 303, exact in
  2703 ms, links 2+2, none ended, 1 ok/4 refused; 3500 ms -> gap 3552, dropped 606, exact in
  4194 ms, 1 ok/4 refused; host 2's link loop's longest turn = the build (1517, 3532 ms), host
  1's 4-5 ms. CI's line in every field, drops in proportion to the hold (~200/s both).
- Plain, 5 runs each: main+probe: the copy 12-16 ms on host 2's loop; host 2's longest turn
  until arrival 16-38 ms, host 1's 4-21 ms. Fix: 0 replies on the loop; host 2's 6-14 ms, host
  1's 3-8 ms. Gap 106-111 ms both. Fix + knob 3500: passes, host 2's 7 ms.
- Not proven: which end held in CI (no stack in those logs); the drops count either end's hold.
  At main the copy is the longest work on either link loop, and removing it cuts host 2's
  longest turn 2-3x; the residual 6-14 ms is TLS and frame work, not a single bulk handler.

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

1. Lead: read the queued jobs and CI 45557132080 (scripts in ~/.scratch/detent/consensus-fetch-alongside-loop-stall must stay until they run).

## Blocked on

Nothing. Open for the lead, not done (a contract change): FetchReader returning shared bytes
(std::shared_ptr<const std::string>) so no host copies a reply on a connection loop; only if
production readers serve large files from memory (peer-fetch.md already says to serve from memory).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | groups: 261124-212647-bda88f (signature.sh at consensus:10747e0, SLOW_READ=6000: node 2's loops 0/1 held >= 6 s, journal 0/0, stall < 1 s, links gone, 2 threads in serve_fetch's reader). Measurement: lags at main 261124-212720-d82038, at the fix 261124-212804-eb47f1. rpc_host: 261124-215634-5e9c39 (rpc-signature.sh 3500 at ee77b26) and 261124-215638-369ffc (1500): fails on a gap >= the hold, exact, links 2+2, none ended, built on host 2's loop, its longest turn >= the hold. Measurement: lags at main+probe 261124-215714-ba0954, at the fix 261124-215644-81f10e. Cause: Doing |
| A2 | groups: 261124-215719-47582c (SLOW_READ=6000 passes at 4fa72a5); fails without: 261124-212704-2b98ad (same at 10747e0). rpc_host: 261124-215646-b613dc (SLOW_READ=3500 passes at 4fa72a5); fails without: 261124-215649-56b54a (same at ee77b26). Bounds unchanged: 1af7530 and 4fa72a5 touch no check |
| A3 | groups 261124-215720-0be582, rpc_host 261124-215716-07805b (25 runs beside 4 burners, strict, at 4fa72a5) |
| A4 | suite 261124-215723-0d3fbd (campaign verify at 4fa72a5); CI 45557132080 |

Superseded, not failures: 261124-212651-a6ec88, 261124-212655-c6962c, 261124-212659-d0b200,
261124-212724-a26bae, 261124-212808-829111, 261124-212812-9fd35e, 261124-212822-225726 (at
ee78a29 or earlier, replaced), 261124-215641-e3eb98 (replaced by ba0954, its env names it).

## Result

Cause: the case's fetch reader built each 8 MiB reply (a byte loop, ~15 ms here) on the loop
serving the fetching node, which also carries that node's Control link; CI's macos-x86_64
runner ran it ~7 s, so node 2's links ended at dead_after and commits stalled. Fix
(test/transport/groups_test.cc only, consensus:1af7530): each round's replies are built before
its fetches go out and the reader moves one out, as docs/mechanisms/peer-fetch.md asks of a
host. Serving loops' longest turn: 10-18 ms at main, 1-4 ms at the fix (lags jobs); with the
reply 6 s slow to build the fix passes and main fails. Every check and bound unchanged. A CI
failure now prints each loop's run state and CPU around the stacks. rpc_host `fetch` (main's 3
macos-x86_64 failures) had the same shape: its reader copied the 16 MiB reply on host 2's loop
serving host 1; stretching that copy gives CI's line in every field (gap, links 2+2, none
ended, drops in proportion). Fix (test/transport/rpc_host_test.cc, consensus:4fa72a5), the same
way: replies built before the loops start, the reader moves one out. Host 2's link loop's
longest turn: 16-38 ms at main, 6-14 ms at the fix; the slow reply passes. Bounds unchanged.

## Rulings I made

- The fix is in the test, not the transport: the transport's contract runs the reader on the
  connection's loop and peer-fetch.md tells hosts to serve from memory; this case's reader
  computed its reply there. Moving the reader off the loop would change the reader's thread
  contract (the lead's).
- The reproduction injects time into building node 2's replies, wherever they are built: the CI
  sample put the held loops inside that build, so the knob slows the work CI saw slow. At the
  fix the build runs on the main thread, between commands; no commit's time counts it.
- Replies are built per round (4 x 8 MiB at a time), not all 16 up front (128 MiB).
- rpc_host builds all its replies (16 + 48 + 5 x 4 MiB) before the loops start, huge and slow
  included, so the reader is one shape for every name; the knob's sleep then runs before the
  heartbeats start and counts in no gap.
- The lead widened the file set to rpc_host_test.cc (home 2a5bd460); the old Blocked-on option
  (a) is done; (b) stays the lead's.
