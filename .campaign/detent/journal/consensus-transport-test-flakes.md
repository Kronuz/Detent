# consensus-transport-test-flakes: journal

State: waiting on jobs 261121-143436-5d5a0f 261121-143440-4dd232 261121-143443-734524 261121-143447-430e88 261121-143458-1278dc 261121-143501-bfbd76 261121-143505-802e52 261121-143509-02c420
Quest: .campaign/detent/quests/consensus-transport-test-flakes.md (the lead's; never edit it)

## Doing

Fix pushed (consensus branch quest/detent/consensus-transport-test-flakes, 14acad3 consensus:14acad3). Jobs
below run the repeat counts. Local numbers are in Result.

## Next

1. Read the jobs' results; fill the Evidence table; `campaign verify` for the suite.
2. When node-admission lands: merge consensus main (connection_test.cc, links_test.cc, CMakeLists.txt are
   Xapiand's until then; not touched here), re-run the jobs at the merged head.
3. A3 (ten CI passes of both tests on linux-tsan and macos-x86_64): CI runs on the pushed branch, the lead's
   count; I list run ids as they come.

## Blocked on

Nothing. Not a transport defect (see Result), so no widened file set is asked for. One doc line is
stale outside my file set: docs/mechanisms/rpc-lane.md:176 says group-full's receiving loop "stalls 4 s"
(now held until the calls end), and its `fetch` row says "about 1.1 s beside 131 MiB ... 210 ms longest
gap" (now measured below). Lead's call whether to widen to docs/mechanisms/rpc-lane.md.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Result below; jobs 5d5a0f (pre-fix fetch fails), 734524 (pre-fix group-full fails) |
| A2 | fetch TSan: 5d5a0f at consensus:329e7f6 (pre-fix, `--fails-without`) vs 4dd232 at the fix; group-full: 734524 vs 430e88 (5 s SIGSTOP at 23 offsets); repeats 1278dc, bfbd76, 802e52; control 02c420 (rpc-shared-credit still fails group-full) |
| A3 | <CI run ids, the lead's count> |
| A4 | <CI> |

Messages: msg-1 (lead: node-admission edits connection_test.cc, links_test.cc, CMakeLists.txt, lands first):
left those alone; my change is rpc_host_test.cc, rpc_hosts.h and two scripts only; will merge consensus main
after it lands.

## Result

fetch under TSan: not a transport defect; the stalls were the test's own, on the connection's loop.
- Cause (local TSan build, macOS x86_64, 16 MiB reader timed in the handler): bytes_at(0, 16 MiB) in the
  reader took 495-690 ms under TSan (22-35 ms as a copy of a made string); the reader runs inline on the
  connection loop, which on host 2 also owns the Control connection to node 1 (loop_of_node(1) = 0, the
  heartbeat group's own loop is 1), so heartbeats sat behind it: every worst gap fell at the fetch's start.
  Each upload burst was one handler of 32 x 1 MiB (140-200 ms under TSan, same loop).
  CI (run 44940676266): gap 1358 ms, fetch 28.6 s; (44924678818): gap 2638 ms, fetch timed out 30 s.
- The gap does not grow with the fetch: 16 MiB over 5-10 s beside 100-260 MiB of uploads gave the same
  200-380 ms at the fix; a starving transport would grow with the volume. It is a unit of work plus the
  200 ms the test itself did not heartbeat before the fetch (plain gap floor was 200 ms: the pre-roll).
- Fix: expected bytes made before the loops start; an upload burst sends one frame per turn; heartbeats
  run through the pre-roll. TSan, 12 runs 3 at once (local): base 8 of 12 failed, gaps 456-2075 ms;
  reader fix only 0 of 12, gaps 377-842 ms; full fix 0 of 12, gaps 211-380 ms; fetch 2.5-3.2 s plain,
  4.3-5.8 s TSan against 4-18 s before. The bound stays 1,000 ms, the election timeout it protects.
- Noted, not a defect: the reader runs on the loop, so a reader that blocks delays that loop's
  heartbeats by its duration (docs/mechanisms/rpc-lane.md).

group-full on macOS x86_64: a runner that pauses the process, not the transport.
- CI logs (macos-x86_64): run 44924240551 job 136907325526: "credit full (no, 458927 bytes uncredited ...);
  15 of 15 calls, largest latency 14 ms; group 1 delivered 100 as the calls ended", 5.94 s against 4.11 s
  normal; run 44906921960 attempt 1 job 136855916052: "no, 3015806 ...; 14 of 15 calls ... delivered 99",
  19.63 s. Both overlapped groups_fetch-alongside-consensus, which failed in the same window ("slowest
  2022 ms", 12.07 s): the runner (3 vCPU, ctest --parallel 2) lost seconds.
- Cause: the 4 s stall was `sleep_for(4000ms)` and `until(full, 2s)` and `until(done, 5s)` were wall-clock
  budgets, so a pause of the process expired them: the group's frames were delivered before the calls
  (stalled 100) or a call timed out. Not seen in 144 plain runs on this Mac at 12 and 32 at once (0 failures
  at the base), so forced: SIGSTOP 5 s at offsets 0.10-1.20 s (test/transport/pause.sh): base failed 16 of
  23 offsets, signature "credit full (yes ...); delivered 100 as the calls ended"; fix 0 of 23.
- Fix: the loop is held until the calls end (released by the test), waits are 30 s limits that are not
  asserted, and the latency bound is 250 ms plus Lag (the longest a 1 ms sleeper thread went without
  running, which only the host can cause). The control `rpc-shared-credit` still fails group-full (job
  02c420): a transport that parks calls behind the credit costs the 10 s call deadline, not milliseconds.

## Rulings I made

- Kept the 1,000 ms bound in fetch: the gaps at the fix are 211-380 ms plain-and-TSan with the cause
  removed, so no TSan factor or bigger number is needed.
- Lag allowance only in group-full (the test whose failure was a pause); call-full and stream carry the same
  `beats.worst < 1000` style bounds and no failure here, so left alone. Candidates if they flake.
- Pre-fix commit for `--fails-without` is consensus:329e7f6, HEAD's tree with the base's rpc_host_test.cc and
  rpc_hosts.h (on no branch), the same way rpc_mutants.py makes a mutant.
