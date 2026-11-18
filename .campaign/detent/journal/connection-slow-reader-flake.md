# connection-slow-reader-flake: journal

State: waiting on jobs 261117-205747-39c311 261117-205630-775f61 (probes, queued: machine full of light holds)
Quest: .campaign/detent/quests/connection-slow-reader-flake.md (the lead's; never edit it)

## Doing

Round 3 (session 2), at 8e3b053 (origin/main merged) and 3d2685e (trace):
- 34479e and 19718e ran 9f8aa7b, a commit later reset off the branch (17e1a34 is its parent);
  the sizing is the same. 34479e: 4 of 60 coarse_writer 48 of 48 in 6453-6456 ms, 0 drained
  expiries, held 231-234 ms, sndbuf 1584204, lowat 1572864, poll events 0x10 at the last sample.
- 0x10 is POLLHUP: the last sample is after the writer's SHUT_WR, so it says nothing about a wait.
  The wedge stat now counts POLLHUP as not wedged (3d2685e).
- Trace 3d2685e: every run prints the write bursts (gaps are the writer's waits) and the reader's
  SO_RCVBUF at accept and end; hypothesis under test: the reader's buffer grows past 512 KiB, so
  the writer never waits dead_after.
- Mutants: eb211c (transport-idle-cost, "passed") configured without `-G Ninja`: 57 tests, and
  CMakeLists registers consensus_mutants only under Ninja. It never ran mutants, so it is no
  baseline. Probe 775f61 (fresh checkout at 8e3b053) runs the first mutant's compile as
  tools/mutants.sh does and prints its build.log; if it fails there, it is main's (this branch
  touches only connection_test.cc), and a job at origin/main shows it.

Next on resume: read 39c311's bursts for passing vs failing runs; read 775f61's build.log; then
fix cause 2 (with a forcing at 17e1a34), job at origin/main for mutants, suite, x200, CI.

Round 1 (consensus 51601dd, forcing knobs): 4c4173 forced cause 1, 3 of 3 failed at line 724
(0 of 64, dialer 2, acceptor 3, 449-459 ms, stalled 3-4 ms, held 0-2 ms: the CI signature).
207149 coarse_writer x24 at 4-way on this Intel Mac passed 24 of 24: sndbuf 1584204, lowat 1572864,
1 expiry each, 6.3 s. So this Mac does not show causes 2 and 3; the CI runner's kernel differs.
f6a66f and 149085 never ran their cases (xargs -I limit); resubmitted below.

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
| A1 | Cause 1: 4c4173 (3 of 3 fail at main 51601dd, READ_SLOWLY_BUILD_MS=400), f52a2f (0 of 3 at 72bd52f). Cause 2: 55466b (3 of 3 fail at main, COARSE_WRITER_SNDBUF=512 KiB), 0c344d (0 of 3 at 72bd52f), 4103a5 (0 of 3 at 17e1a34). Cause 3: unreproduced, see above (A1 amended). |
| A2 | 261117-193218-d85e3b (x200 at 4-way at 72bd52f; six shards passed); 261117-202032-34479e (x60 at 17e1a34, the head, differing only by the revert and the dump) |
| A3 | [manual] every cause is in connection_test.cc's sizing and build order, none in the product (connection.h unchanged). |
| A4 | Suite 261117-202041-19718e (fresh checkout) at 17e1a34; CI green on every platform at 72bd52f (lead), CI at 17e1a34 pending. consensus_mutants (0a9502) mutates core.h and builds the simulation tests, never connection_test.cc; it failed in a slot my targeted jobs had configured Release. |

## Result

## Rulings I made

- Forcing knobs are environment variables read by the test (`READ_SLOWLY_BUILD_MS`,
  `COARSE_WRITER_SNDBUF`, `COARSE_WRITER_SHRINK`); default off.
