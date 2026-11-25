# consensus-fetch-alongside-loop-stall: journal

State: ready for review (correctness CHANGES F1-F3 folded in at consensus:0d8263d; jobs queued: 261124-232245-034e6c 261124-232249-6e4d93 261124-232254-7d11cf 261124-232254-6b4d7b 261124-232255-8bc466 261124-232255-0a3ff3 261124-232300-fe7353; CI 45567716437)
Quest: .campaign/detent/quests/consensus-fetch-alongside-loop-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Both fixes done; review findings F1-F3 folded in and pushed (consensus:0d8263d; CI 45567716437). Commits:
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
- consensus:e17d9d8 main b4516ee merged.
- consensus:019f63c F1: print_stacks marks its window (dumps, odd while one runs); a Stalls nap
  that overlaps it does not count as the host's stall.
- consensus:4898f1c F2: rpc_host's 4 MiB replies checked by value against
  expected.substr(0, 4 << 20); the 16 MiB fetched again after the table, exact.
- consensus:0d8263d F1: that nap goes to dump_stall, printed beside the stall ("a stack dump,
  not counted: N ms").

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
- NOT ROOT-CAUSED (review F3): CI's logs do not say which end held, and the drops count either
  end's hold. Host 1's requester also copies in one turn (its unreserved append doubles, up to
  8 MiB at once; host_transport.h:1486). The jobs show only that a held reader is sufficient
  for CI's line; lags overlap (main+probe 13-25 ms, fix 3-32 ms, jobs ba0954, 81f10e). The
  change stands as test hygiene (no 16 MiB build on a link loop); the Turns line names the
  held end on the next macos-x86_64 failure.

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

1. Lead: read the queued jobs and CI 45567716437 (scripts in ~/.scratch/detent/consensus-fetch-alongside-loop-stall must stay until they run; mutants 43ebf5f and 864192a are unreferenced commits in the consensus object store).

## Blocked on

Nothing. Open for the lead, not done (a contract change): FetchReader returning shared bytes
(std::shared_ptr<const std::string>) so no host copies a reply on a connection loop; only if
production readers serve large files from memory (peer-fetch.md already says to serve from memory).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | manual (who: the lead, 2026-11-25; checked: the reproductions run on pre-fix code by design. groups job ...232245-034e6c at consensus:43ebf5f, the reader still building on the loop, gave CI's link signature; rpc_host jobs ...215634-5e9c39 and ...215638-369ffc at consensus:ee77b26 gave 1537 and 3552 ms gaps against CI's 1564 and 3746-3765 ms with no link ended. At the fix the same knobs no longer stall the loop (reruns ...cb3ac9 and ...caeef6 do not reproduce). rpc_host stays not root-caused, F3) |
| A2 | groups: 261124-232254-7d11cf (SLOW_READ=6000 passes at 0d8263d); fails without: 261124-212704-2b98ad (same at 10747e0). rpc_host: 261124-232255-8bc466 (SLOW_READ=3500 passes at 0d8263d); fails without: 261124-215649-56b54a (same at ee77b26). F2: fails without 261124-232249-6e4d93 (plain fetch at consensus:864192a, the head without the reader's pop_back). Bounds unchanged: 1af7530, 4fa72a5, 019f63c, 0d8263d touch no bound; 4898f1c adds checks |
| A3 | groups 261124-232254-6b4d7b, rpc_host 261124-232255-0a3ff3 (25 runs beside 4 burners, strict, at 0d8263d) |
| A4 | suite 261124-232300-fe7353 (campaign verify at 0d8263d); CI 45567716437 |

Review probes, not mine: 261124-220716-08aef6 (moved-out mutants; rpc_host missed it, F2),
261124-222251-e201f1 (dump stall, F1).
Superseded, not failures: 261124-212651-a6ec88, 261124-212655-c6962c, 261124-212659-d0b200,
261124-212724-a26bae, 261124-212808-829111, 261124-212812-9fd35e, 261124-212822-225726,
261124-215641-e3eb98, and the 4fa72a5 runs 261124-215719-47582c, 261124-215720-0be582,
261124-215646-b613dc, 261124-215716-07805b, 261124-215723-0d3fbd (passed; rerun at 0d8263d).

## Review findings (correctness, CHANGES at 4fa72a5)

- F1 (blocking): class = a diagnostic that changes what the case measures. The dump's `sample`
  counted as the host's stall (widening the bound and failing the signature's stall check).
  Locally (signature at 43ebf5f, 9 runs) the dump suspended nothing in 8 (dump nap 4-8 ms,
  dump 1.5-2 s, 4 peers gone each); in 1 node 2 had 0 peers gone. A suspending dump can let
  links outlive dead_after: the run now prints it, and the signature reruns only such a run.
  Every Stalls user goes through print_stacks (report_hang too), so one mark covers them.
  Test: 261124-232245-034e6c (failed before: bda88f).
- F2: class = a reply counted, not compared. All of rpc_host fetch's replies now compare by
  value (big twice, the 4 MiB ones); missing/throws/huge are errors. Test: fails without
  261124-232249-6e4d93.
- F3: recorded above as not root-caused; the Result claims only what the jobs show.
- F4: the lead's (product). F5, F6: nits, not done (the lead listed F1-F4).

## Result

Cause: the case's fetch reader built each 8 MiB reply (a byte loop, ~15 ms here) on the loop
serving the fetching node, which also carries that node's Control link; CI's macos-x86_64
runner ran it ~7 s, so node 2's links ended at dead_after and commits stalled. Fix
(test/transport/groups_test.cc only, consensus:1af7530): each round's replies are built before
its fetches go out and the reader moves one out, as docs/mechanisms/peer-fetch.md asks of a
host. Serving loops' longest turn: 10-18 ms at main, 1-4 ms at the fix (lags jobs); with the
reply 6 s slow to build the fix passes and main fails. Every check and bound unchanged. A CI
failure now prints each loop's run state and CPU around the stacks. rpc_host `fetch` (main's 3
macos-x86_64 failures): NOT root-caused. A reader held on host 2's link loop is sufficient for
CI's line (gap, links 2+2, none ended, drops in proportion), but the logs do not name the held
end, and host 1's requester also copies in one turn. The change (consensus:4fa72a5, the same way:
replies built before the loops start, the reader moves one out) stands as hygiene; the case now
checks every reply by value and fetches the 16 MiB twice, and its Turns line names the held end
next time. Bounds unchanged. A stack dump's suspension is no longer counted as the host's stall
(F1) and is printed apart.

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
