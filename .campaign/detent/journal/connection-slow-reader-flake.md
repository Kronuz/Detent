# connection-slow-reader-flake: journal

State: waiting on jobs 261117-201826-b83a81
Quest: .campaign/detent/quests/connection-slow-reader-flake.md (the lead's; never edit it)

## Doing

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

1. Read suite b83a81 (a fresh checkout). If it passes, State ready for review.
2. The lead rules on cause 3 (Blocked on).

## Blocked on

Lead: A1 for cause 3. Job 876ce5 (COARSE_WRITER_SHRINK at main) did not reproduce: 0 of 3 failed.
The kernel clamps the low-water mark to the buffer at every set (BSD sosetopt), so a mark above the
buffer cannot exist here (readback after the shrink: sndbuf 1061580, lowat 1048576). The cause-3
hypothesis was wrong; the 72bd52f mark (SO_SNDBUF - 16 KiB after connect) is harmless but proves
nothing. The CI signature (13 of 48, dialer failed, acceptor dead, queue empty for 2 s, 0 expiries,
44224758109) is unexplained and has no forcing; it failed once in CI, never on this Intel Mac
(24 of 24 passed), and CI is green at 72bd52f (one sample). Options: (a) accept A1 for causes 1-2 and
record cause 3 as unreproduced (recommended: no code path in the product or the test explains it);
(b) add a post-failure diagnostic dump (queue, SO_ERROR, poll() events of the dialer's fd) and wait
for it to recur in CI.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Cause 1: 4c4173 (3 of 3 fail at main with READ_SLOWLY_BUILD_MS=400), f52a2f (0 of 3 at the fix). Cause 2: 55466b (3 of 3 fail at main with COARSE_WRITER_SNDBUF=512 KiB), 0c344d (0 of 3 at the fix). Cause 3: 876ce5 did NOT fail at main (0 of 3); 1d47a2 passes at the fix, trivially. See Blocked on. |
| A2 | 261117-193218-d85e3b (connection x200 at 4-way; lead: six shards passed) |
| A3 | [manual] every cause is in connection_test.cc's sizing, none in the product (connection.h unchanged). |
| A4 | CI green on every platform at 72bd52f (lead); suite 0a9502 failed consensus_mutants only: tools/mutants.sh mutates core.h and builds the simulation tests, it never touches connection_test.cc; all its builds failed together in a build dir my targeted jobs had configured Release with CONSENSUS_TRANSPORT=ON. Rerun in a fresh checkout: b83a81. |

## Result

## Rulings I made

- Forcing knobs are environment variables read by the test (`READ_SLOWLY_BUILD_MS`,
  `COARSE_WRITER_SNDBUF`, `COARSE_WRITER_SHRINK`); default off.
