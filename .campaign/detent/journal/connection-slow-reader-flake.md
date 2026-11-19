# connection-slow-reader-flake: journal

State: waiting on jobs 261118-223426-9ebaf9, 261118-223429-3998b5 (A1), 261118-223432-13d2c9, 261118-223436-ed18de, 261118-223440-f7b703 (A2), 261118-223443-6c49b0 (suite), CI 44473145978, all at 1bace1e (refuses a mark the reader can outpull, before the run).
Quest: .campaign/detent/quests/connection-slow-reader-flake.md (the lead's; never edit it)

## Doing

Round 8, 2e6334 (head 9a8ebf4, 896 KiB) failed 3 of 3: a real failure, not the old check refusing.
Runs 1-2 got past the precondition and ended stuck: dialer failed, 9 of 48 frames, the queue held
4914 ms without draining at a 994 ms pace (bursts 2:0->918810, 2004:908904->925310). Run 3's check
printed -19126 B spare: mark 917504 over capacity 653696 leaves 250388 B above the wake level, and
at 994 ms the reader pulls up to 278528 B a dead_after (3 frames, a half-read, a record). Defect: the
pace formula left out the half-read frame and the record, so at a small mark it gives a case that
cannot outlast dead_after. Fix 1bace1e: compute that bound from the pace and fail by value before
the run when the mark leaves no room for it. Default: 606208 < 1035988, unchanged (1.71x). 896 and
512 KiB both refused before the run (checked once by hand, then 3998b5). Why the queue showed no drain
for 4.9 s at 994 ms is not explained; the check now refuses that case.
Honest shape for cause 2: 3049dd (512 KiB at main fails 3 of 3) plus 3998b5 (the head refuses the
512 KiB mark by value, 3 of 3). No forcing fails at main and passes at the head on this Mac: 1 MiB
passes at main (2bfc30), and 896 KiB passes at main (81f711) and is refused at the head. The 896 KiB arm
is dropped.

Round 7 bookkeeping (lead's message after round 2):
- Main merged (9a8ebf4: groups_test.cc only).
- 2bfc30 (1 MiB at main) failed 0 of 3: main passes at 1 MiB (2 expiries, 6.4 s), so 1 MiB is no
  forcing. 512 KiB fails at main but is refused by value at the head (mark within the 653696 B
  capacity). Forcing moves to 896 KiB: head pace 994 ms, 40 frames, ~40 s; 2e6334 at the head, 81f711
  at main. 3049dd keeps 512 KiB at main. The main jobs are now --fails-without (exit nonzero only
  when all 3 fail); 4c4173/55466b and their head reruns d59b1b/7b7c57 are superseded by
  dc092b (cause 1 at main) and 3049dd.

Round 7, the precondition at 4050afc (aa0e71 120 of 120, c0c368, CI 44458203198 macos-arm64, macos-x86_64):
"SO_RCVBUF 524288 pinned, 1046912 sampled at most ... -88448 B spare". Every one of those runs passed
48 of 48 with 2 drained expiries: only the check failed.
- Measured (fill probes, .scratch/rcvprobe, this Mac, 15.6 build 24G): a loopback reader pinned at P
  holds P + 522624 B, at every pin tried (131072 -> 653696, 174762 -> 697386, 262144 -> 784768,
  524288 -> 1046912), any send buffer (512 KiB, 1.5, 3 MiB), readback and FIONREAD agree; it grows
  within 3 ms of the first data. Not 2x: 2x the readback would hold at 512 KiB with 1664 B to spare
  and fail at every smaller pin. autorcvbufmax is 4 MiB, so not a ceiling either.
- Fix (830f04a): reader_capacity(pin) fills a reader at the same pin once before the run, capacity =
  max(FIONREAD held, readback). Pin 128 KiB. The writer's buffer and mark = capacity + 1 MiB (knob
  overrides), pace = 2 dead_after x size / (lowat - capacity) = 250 ms, frames = (sndbuf + capacity)
  / size + 16 = 52 (13.5 s). Checks: sampled SO_RCVBUF <= capacity, spare = peak - capacity - pulled_max
  - wake > 0, and lowat > capacity before the run; each a hard failure by value, no rerun. The
  precondition line prints on every coarse run ("precondition holds/broken").
- Headroom here: capacity 653696, sampled 647630 at most; peak 1707988, pulled 606208, wake 12588:
  435496 B spare, (peak - capacity - wake) / pulled = 1.72x. Knob 1 MiB at the head: pace 664 ms,
  32.3 s, 9 expiries, 115854 B spare (1.42x). Knob 512 KiB at the head now fails by value: the mark
  (524288) is within the capacity (653696), so no pace makes the wait certain; cause 2's forcing moves
  to 1 MiB (2bfc30 at 51601dd must fail 3 of 3; 55466b, 512 KiB at main, stands).
- Mutant df12a1 "failed" because its build lacked the TLS fixture (use_certificate_chain_file abort),
  not the check: script fixed (ctest -R consensus_tls_certificates), resubmitted as cc66bd.

Round 6, the precondition at f116bfa (29d81b, a2966e, CI 44445906462 macos-arm64 and macos-x86_64):
"SO_RCVBUF 538956 pinned, 1046912 sampled at most; reader pulled 606208 B; -88448 B spare".
- Buffer: the sampler reads the reader's own socket, so the kernel did grow it: 1046912 is ~2x the
  readback, the macOS autotune ceiling (1 MiB). The pin was set on the listener, which the accepted
  socket inherits as a size but not as a user-set size, so autotune ran. Fix: set SO_RCVBUF on the
  accepted socket (4050afc's parent), readback taken after. Not seen on freebsd-x86_64: its job failed
  in the VM's package update ("Error updating repositories", ssh exit 3), before any test ran.
- Pace: 606208 = 9 x 65536 + 16384, the checker's worst case, not a measured 296 KB/s: 8 frames
  fit a 2 s window at 254 ms, plus the frame the reader holds half-read, plus one 16 KiB record. The
  reader pulls no more than that (fill reads 16 KiB until one whole frame is in the buffer, then
  delivers each frame, sleeping 254 ms), so there is no burst to cap. With the buffer at 538956 the
  spare is 1576012 - 538956 - 606208 - 11340 = 419508 B (about 0.6 of the queue). No pace change.
- Suite ad8107 timed out (124): consensus_mutants (1650 s in 69ed8e, which ran 1804 s of its 30 min) was
  the test left running, with 6 busy loops of 29d81b on the host; connection_test is 18.6 s of it (13.2
  s before; coarse_writer 12.6 s against 6.2 s: 48 frames x 254 ms, the floor the pace sets). The
  suite's 30 min bound is too tight for mutants whatever this quest does: the lead's.
- Mutant 93b72b passed while the head failed (it only needed a precondition message): redone as df12a1
  at a head that should pass.

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
| A1 | Cause 1: 261118-221112-dc092b (--fails-without at main 51601dd, READ_SLOWLY_BUILD_MS=400, 3 of 3 fail), 261118-223426-9ebaf9 (0 of 3 at 1bace1e). Cause 2: 261118-221115-3049dd (--fails-without at 51601dd, 512 KiB, 3 of 3 fail), 261118-223429-3998b5 (the head refuses a 512 KiB mark by value before the run, 3 of 3). Cause 3: unreproduced (A1 amended). Replaced, not cited: 4c4173, d59b1b, 55466b, 7b7c57, ca13b5, 2bfc30, 81f711 and 2e6334 (896 KiB, dropped), and the d9fca7d runs 6afa43, b4fac6, e7bfa3, e4f648, f43386, f98a6a. |
| A2 | 261118-223432-13d2c9 (connection x200 at 4-way, 1bace1e); 261118-223436-ed18de (coarse_writer x120 at 4-way, 6 busy loops); mutant 261118-223440-f7b703 (--fails-without: unpaced reader stops on "precondition broken"). At 830f04a: 61ad72, e3cfba, cc66bd passed. |
| A3 | manual (who: the contributor, connection-slow-reader-flake; checked: `git -C consensus diff origin/main...1bace1e --stat` changes only test/transport/connection_test.cc, so include/consensus/transport/connection.h and all product code are unchanged; each named cause is in the test: the backlog built on the running dialer loop, sizing from an assumed send buffer, the reader's capacity past its pin, a pace that ignored the half-read frame) |
| A4 | Suite 261118-223443-6c49b0 at 1bace1e (5d017a passed at 830f04a); CI 44473145978 at 1bace1e (44464145833 and 9a8ebf4's CI green). |

## Result

830f04a (test/transport/connection_test.cc): coarse_writer is sized from the reader's measured
capacity and fails by value when the sampled buffer exceeds it, the spare is gone, or the mark sits
within it. Cause 2's old 4-of-60 (34479e) stays unexplained: the extra 510 KiB is a candidate, not shown. Cause 3 stays unreproduced.

## Rulings I made

- Forcing knobs are environment variables read by the test (`READ_SLOWLY_BUILD_MS`,
  `COARSE_WRITER_SNDBUF`, `COARSE_WRITER_SHRINK`); default off.
- Round 7: the lead's option 2 (measure the capacity by filling a reader once, size the queue from
  it), not 2x the readback: 2x is wrong at every pin but 512 KiB (P + 522624 measured). Pin 128 KiB
  keeps the pace at 250 ms and the run at 13.5 s. Cause 2's head forcing uses 1 MiB, since 512 KiB
  is now refused by value on macOS (mark within the capacity).

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
