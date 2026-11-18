# connection-slow-reader-flake: journal

State: waiting on jobs 261117-193124-55466b 261117-193135-876ce5 261117-193147-f52a2f 261117-193157-0c344d 261117-193207-1d47a2 261117-193218-d85e3b 261117-193228-0a9502
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

1. Read the round-2 jobs; if a main job passes (fails to fail), the forcing does not reproduce that cause: rework it.
2. Read CI at 72bd52f (macos-x86_64, linux-tsan); confirm causes 2 and 3 or revise.
3. Paste `campaign brief`'s Evidence table; State ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

## Rulings I made

- Forcing knobs are environment variables read by the test (`READ_SLOWLY_BUILD_MS`,
  `COARSE_WRITER_SNDBUF`, `COARSE_WRITER_SHRINK`); default off.
