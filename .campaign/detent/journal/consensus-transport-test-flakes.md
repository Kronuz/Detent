# consensus-transport-test-flakes: journal

State: waiting on CI run 44989408833
Quest: .campaign/detent/quests/consensus-transport-test-flakes.md (the lead's; never edit it)

## Doing

Merged consensus main (e35356a, consensus_mutants per-test) into the branch and pushed: head consensus:4536590.
Pre-fix jobs rerun at consensus:2c1abbb (head's tree with main's rpc_host_test.cc and rpc_hosts.h; on no branch).

## Next

1. When run 44989408833 ends: record it; rerun the workflow one run at a time (`gh run rerun ID -R Kronuz/consensus`)
   until 3 green runs at 4536590 (A3 is three in a row now); list each id in A3.
2. (done) 261121-212631-ac045b and 261121-212632-5bcdd7 failed as designed.

## Blocked on

Nothing. Not a transport defect (see Result), so no widened file set is asked for.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [artifact] Result below; reproducing jobs 261121-143436-5d5a0f (fetch under TSan, pre-fix fails), 261121-143443-734524 (group-full paused, pre-fix fails) |
| A2 | pre-fix at the head's tree: 261121-212631-ac045b (fetch), 261121-212632-5bcdd7 (group-full), both --fails-without at consensus:2c1abbb, each exited 1 as designed; earlier fetch TSan: 261121-143436-5d5a0f (pre-fix, fails) vs 261121-143440-4dd232 (fix, 48 runs, gaps 125-258 ms) and 261121-143505-802e52 (plain, 48 runs, gaps 106-108 ms); group-full: 261121-143443-734524 (pre-fix fails, 5 s pause at 23 offsets) vs 261121-143447-430e88 (fix passes); repeats 261121-143458-1278dc (TSan 48), 261121-143501-bfbd76 (plain 96); control 261121-143509-02c420 (rpc-shared-credit: "0 of 15 calls ... 100 of 100 after FAILED", 2 of 2 runs failed: the expected control result, a transport parking calls behind the credit) |
| A3 | CI runs at consensus:4536590: 44989408833 (queued). Earlier, run 44957530000 at consensus:14acad3: rpc_host_fetch and rpc_host_group-full Passed on linux-tsan (1.97 s, 4.36 s) and macos-x86_64 (2.21 s, 3.44 s) and all 8 other jobs; one run of ten. Waiting on the lead's mutants fix for green runs. |
| A4 | CI run 44957530000: every red job (freebsd, asan-ubsan, tsan, macos-x86_64, arm64-gcc) failed only consensus_mutants (Timeout 1800 s / Failed 0.00 s, 1 of 113); the rest passed. Needs consensus-mutants-per-test |

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
  heartbeats by its duration.

group-full on macOS x86_64: a runner that pauses the process, not the transport.
- CI logs (macos-x86_64): run 44924240551 job 136907325526: "credit full (no, 458927 bytes uncredited ...);
  15 of 15 calls, largest latency 14 ms; group 1 delivered 100 as the calls ended", 5.94 s against 4.11 s
  normal; run 44906921960 attempt 1 job 136855916052: "no, 3015806 ...; 14 of 15 calls ... delivered 99",
  19.63 s. Both overlapped groups_fetch-alongside-consensus, which failed in the same window ("slowest
  2022 ms", 12.07 s): the runner (3 vCPU, ctest --parallel 2) lost seconds.
- Cause: the 4 s stall was `sleep_for(4000ms)` and `until(full, 2s)` and `until(done, 5s)` were wall-clock
  budgets, so a pause of the process expired them: the group's frames were delivered before the calls
  (stalled 100) or a call timed out. Not seen in 51 plain runs of the base on this Mac (48 at 12 at once; 0 failures), so forced: SIGSTOP 5 s at offsets 0.10-1.20 s (test/transport/pause.sh): base failed 16 of
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
- docs/mechanisms/rpc-lane.md (widened into the file set by the lead): group-full and fetch rows now state what
  the test asserts and the measured gaps (consensus:3ae265a).
