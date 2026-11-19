# connection-slow-reader-flake: journal

State: waiting on jobs 261118-183926-29d81b (coarse_writer x120, 6 busy loops), 261118-183927-a2966e (x120, host as is), 261118-183928-93b72b (mutant: no pacing, --fails-without), 261118-183929-ad8107 (suite), CI 44445906462, all at f116bfa. e4ab32 and 4a286b (887dafe, the retry) cancelled. Not yet compiled locally: a build error shows in the first job.
Quest: .campaign/detent/quests/connection-slow-reader-flake.md (the lead's; never edit it)

## Doing

Round 5, lead's ruling on f79a8e (120 of 120, pulse off, d9fca7d):
- What differed at 9f8aa7b (34479e, 4 of 60) from d9fca7d: connection.h unchanged; read_slowly_once's
  writer, reader and sizing are identical. Only the observation changed (dup'd fds, getsockopt calls,
  the drain sampler's burst log, the pulse, off since). So the code that runs is the same; the host
  differed: 34479e overlapped a qemu VM job (A6, 261117-200358-e66125) and a strict bench.
- Headroom, from 224 traced runs (f79a8e 118, 4c8b1c 106): the writer's first wait expires at 2002-2017
  ms with 546-828 KB queued of 1575050; drain 372-512 KB/s (reader pace 64 KiB/125 ms = 524 KB/s caps it);
  writable needs the queue at <= 11340 B, so at the same rate the wait would end at 3055-4198 ms: at least
  1.53x dead_after (2000 ms), and the factor is the test's own (pace = 1.5 dead_after / mark bytes), not a fit.
  A failing run needed the queue to empty in < 2 s: at least 780 KB/s, 1.5x the fastest drain seen.
  A rcvbuf that autotuned past 512 KiB would give that (not seen: 524288 at the end in every traced run).
- Lead overruled the retry (it hides the next real failure): 887dafe is superseded. f116bfa makes the
  expiry certain by construction, and asserts why:
  1. The reader's SO_RCVBUF is pinned at min(512 KiB, sndbuf/3) on the listener (a set buffer is not
     autotuned; its readback, doubled on Linux, is the pinned size). rcvbuf_max is sampled every 2 ms.
  2. Pace: per_frame >= 2 dead_after x frame / (lowat - rcvbuf) = 250 ms for 64 KiB, 256 KB/s: the reader
     pulls at most half of what is left of the queue in dead_after. The sleep per frame is the cap (a
     token bucket without the bucket: no burst to size).
  3. read_slowly, for a coarse run that wakes at an empty queue, prints and asserts by value before any
     other check: reader SO_RCVBUF max <= pinned, and queue peak - rcvbuf_max - pulled_max - (sndbuf -
     lowat) > 0, where pulled_max is the most the reader pulled in any dead_after (frames its handler took
     in the window, plus one half-read, plus one 16 KiB record). A breach prints "precondition broken" with
     the numbers; no rerun loop remains (the host-stall rerun that predates it stays).
- Mutant 93b72b: the handler's sleep removed; the script exits 1 only if coarse_writer prints "precondition
  broken"; a build failure exits 0 (the job fails), so a pass is the check firing.
- 3 and the headroom figures above (3055 ms, 1.53x) are for the 125 ms pace; at 250 ms the same bound is 2x.

## Causes (all test-side; no product cause, A3)

1. slow_reader 0 of 64 (693/614): the test built the 32 MiB backlog on the dialer's loop after both
   run() had started; slow under TSan and on a loaded Mac, it held that loop, so the dialer sent no data
   or keepalive for dead_after and the reader's dead timer ended dead. Forced: READ_SLOWLY_BUILD_MS=400.
   Fix: queue the backlog and close() before either end runs (72bd52f).
2. coarse_writer 688 (macos-x86_64): 48 of 48 but 0 expiries. The case assumed a 1.5 MiB buffer; a
   smaller one drains in under dead_after, so no wait expires. Forced: COARSE_WRITER_SNDBUF=524288.
   Fix: size pace (max(125 ms, 1.5 dead_after / mark bytes)) and frame count from the connected
   socket's SO_SNDBUF and mark. Inferred from the CI line, not seen on this Mac.
3. coarse_writer 693 (macos-x86_64): 13 of 48, dialer failed, acceptor dead, queue 2 ms: the mark
   (set before connect) above the buffer the kernel gave, so the socket is never writable and the stuck
   rule ends a writer waiting on an empty queue. Forced: COARSE_WRITER_SHRINK=1048576. Fix: set the
   mark to SO_SNDBUF - 16 KiB after connect. Inferred; the 13 frames fit a ~0.9 MiB first burst.

Round 2 at 72bd52f (fix) and 51601dd (main): 55466b, 876ce5 (A1 at main: 512 KiB and shrink, 3 of 3
fail expected), f52a2f, 0c344d, 1d47a2 (A1 at fix: slow_reader, 512 KiB, shrink: 0 of 3 fail),
d85e3b (A2: connection x200 at 4-way), 0a9502 (suite at head).
CI: pushed 72bd52f to origin quest/detent/connection-slow-reader-flake; macos-x86_64 is where 2 and 3
confirm.

## Lead's message (no id listed): groups_upload is out of the quest

groups_test.cc belongs to transport-feed-scan, which is rewriting it; groups_upload gets a follow-up
after feed-scan lands. A2 is now connection x200 only. Done: cancelled the groups_upload job,
no edit to groups_test.cc, cause 4 left to the follow-up. Carrying on with causes 1-3.

## Next

1. Read 4103a5, 34479e and the fresh suite 19718e at 17e1a34 (superseded: b83a81 at 72bd52f).
2. Check CI at 17e1a34; if all green and the jobs pass, State ready for review.
3. If coarse_writer fails again in CI, its new line (buffer, mark, queue, wedged ms, poll events,
   SO_ERROR) names cause 3.

## Blocked on

Nothing. Lead ruled: cause 3 stays unreproduced (below); A1 amended at Detent main.

## Cause 3, unreproduced

CI line, 44224758109 (macos-x86_64): coarse_writer 13 of 48, dialer failed, acceptor dead, queue held
2 ms, 0 expiries, 5057 ms. Hypothesis "the mark above the buffer" is false: job 876ce5
(COARSE_WRITER_SHRINK=1 MiB at main, 51601dd) failed 0 of 3, and its readback was sndbuf 1061580,
lowat 1048576: the kernel clamps the mark to the buffer at every set. The mark change was reverted
(17e1a34) and the knob removed; coarse_writer now prints buffer, mark, queue, the longest empty-but-
unwritable stretch with its poll events and SO_ERROR when it fails. 24 of 24 passed on this Intel Mac
(207149). Next CI occurrence names it.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Cause 1: 4c4173 (3 of 3 fail at main 51601dd, READ_SLOWLY_BUILD_MS=400), f52a2f -> 261118-171202-e7bfa3 (0 of 3 at head d9fca7d). Cause 2: 55466b (3 of 3 fail at 51601dd, COARSE_WRITER_SNDBUF=512 KiB), 0c344d -> 261118-171202-6afa43, 4103a5 -> 261118-171203-f98a6a (0 of 3). Shrink: 1d47a2 -> 261118-171202-b4fac6, dee9ea -> e4f648, 80c40b -> f43386 (0 of 3). The reruns of 4c4173 (d59b1b) and 55466b (7b7c57) "failed" because they ran at head, where the forcing no longer fails: the originals at 51601dd stand. Cause 3: unreproduced (A1 amended). |
| A2 | 261118-171202-5c2349 (connection x200 at 4-way at d9fca7d); 261118-171154-f79a8e (coarse_writer x120 at d9fca7d: 120 of 120); at f116bfa: 261118-183926-29d81b (x120, loaded), 261118-183927-a2966e (x120), mutant 261118-183928-93b72b |
| A3 | [manual] every cause is in connection_test.cc's sizing, build order and a rerun rule, none in the product (connection.h unchanged). |
| A4 | Suite 261118-183929-ad8107 at f116bfa; CI 44445906462 at f116bfa (44435046094 green at d9fca7d). |

## Result

Open: cause 2's 4-of-60 (34479e, 17e1a34's sibling 9f8aa7b) is unexplained: 0 of 242 since at d9fca7d and
the pulse head. Its mechanism would be the queue draining in < 2 s (>= 780 KB/s against 512 KB/s seen),
most likely a reader receive buffer past 512 KiB; the next occurrence prints its rcvbuf and write bursts.
Cause 3 (coarse_writer 13 of 48 on macos-x86_64) is unreproduced. A breach of the two preconditions now fails by value (f116bfa).

## Rulings I made

- Forcing knobs are environment variables read by the test (`READ_SLOWLY_BUILD_MS`,
  `COARSE_WRITER_SNDBUF`, `COARSE_WRITER_SHRINK`); default off.

## Lead's message (reader-yield-stall), answered

Lead: cause 2 may be reader-yield-stall (reader parked 0.4-1.2 s at connection.h's yield, diag
6f15b3). Check against 39c311; if it matches, cause 2 is that quest's, no test fix.
Before the trace: it doesn't fit yet. A stalled reader drains slower, so write waits get longer
and expiries more likely, but 34479e's failures had 0 expiries. Its held was 231-234 ms at most
without draining, so the reader never stalled 0.4 s. The reader is on l2 and the writer's
deadline on l1, so a reader stall can't trigger the writer's late extension either. 39c311
decides it; A2 soak and the dump are kept.

## Probes read (round 3)

- 775f61 (mutant compile, 8e3b053): link fails, undefined
  `driver_simulation::run(unsigned long long, unsigned long long, unsigned long long)` from
  simulation_test's main. Main's 0455f58 (2026-11-16, "Run the simulation through the Driver")
  added test/driver_simulation.cc to consensus_simulation_test (CMakeLists:57); tools/mutants.sh
  still compiles only simulation_test.cc. Main's bug, outside this file set: a small fix to
  mutants.sh (add driver_simulation.cc to its compile) belongs to the lead. 04c323 runs the
  same probe at origin/main 9f86ec3 to prove it.
- 39c311 (trace x60 at 3d2685e): 60 of 60 passed, 1 drained expiry each, 6222-6240 ms, held
  129 ms. Bursts: 2:0->1575050, then 2002-2009:~823704->~1283310. The writer refills only at the
  dead_after expiry: the queue still holds ~800 KB then, so writability (queue <= sndbuf-lowat,
  11340 B) would take ~4 s at the observed ~375 KB/s drain. Normal runs never wake on
  writability before the expiry.
- 34479e's failures differ: 6453-6456 ms (+230 ms, all four within 3 ms) and held 231-234 ms. So
  in a failing run the queue stopped draining ~230 ms once and the expiry never counted.
  Sizing can't explain 0 expiries: the queue can't empty in 2 s at this rate. The open question
  is why the 2 s expiry didn't fire or count. Candidates: (a) the writer's loop fired its wheel
  late, so the late rule extended the deadline; (b) the writer woke early for another reason.
  4c8b1c traces the writer loop's worst late timer and when (35bd913).
