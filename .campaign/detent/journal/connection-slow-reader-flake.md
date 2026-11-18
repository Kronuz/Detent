# connection-slow-reader-flake: journal

State: waiting on jobs 261117-191526-4c4173 261117-191531-207149 261117-191537-084643 261117-191542-f6a66f 261117-191548-149085
Quest: .campaign/detent/quests/connection-slow-reader-flake.md (the lead's; never edit it)

## Doing

Round 1 at consensus-wt `51601dd` (forcing knobs in test/transport/connection_test.cc, pushed to
origin quest/detent/connection-slow-reader-flake for CI). Jobs:
- 4c4173 A1 forcing of cause 1 (READ_SLOWLY_BUILD_MS=400, expect 3 of 3 failures at this commit).
- 207149 groups_upload x16 at 4-way (rate and the "upload:" lines on this Intel Mac, the CI macos-x86_64 class).
- 084643 coarse_writer x24 at 4-way, now printing sndbuf, lowat and queue left.
- f6a66f coarse_writer COARSE_WRITER_SNDBUF=512 KiB x3 (forces cause 2 if it is a short drain).
- 149085 coarse_writer COARSE_WRITER_SHRINK=1 MiB x3 (forces cause 3 if it is a mark above the buffer).

## Causes, from the CI logs (runs 44166915224, 44184143126, 44192856154, 44216299768, 44224758109)

1. slow_reader "0 of 64 frames, dialer 2 (failed), acceptor 3 (dead), stalled 1-60 ms, held 0-20 ms",
   at 316-485 ms, line 693 (linux-tsan, macos-x86_64; 614 is the same assert before the held rule).
   Hypothesis H1: `l1.call` builds 64 x 512 KiB strings and appends 32 MiB to out_ on the dialer's own
   loop; slow under TSan and on a loaded Mac, it holds that loop past dead_after, so the dialer sends
   neither data nor keepalives and the acceptor, whose dead timer runs from its run(), ends dead. The
   host did not stall (stall_watch and drain_watch see nothing); the test starved its own writer.
   Test, not product. Fix: queue the frames before either run() starts.
2. coarse_writer 688 (44216299768, macos-x86_64): 48 of 48 frames, closed, but 0 write waits expired
   with the queue drained although the mark took. Hypothesis: the effective drain time is under
   dead_after on that host (a smaller effective buffer), so no wait reaches dead_after. Unconfirmed.
3. coarse_writer 693 (44224758109, macos-x86_64): 13 of 48, dialer 2, acceptor 3, queue held 2 ms,
   5057 ms, 0 expiries: the writer waited dead_after with an empty queue and was never woken, so the
   stuck rule ended it. Hypothesis: the mark above the buffer (never writable). Unconfirmed; if the
   wake is lost with a coarse mark that the product never sets, that is still the test's fixture.
4. groups_upload fails on macos-x86_64 in 44130104547, 44214025598 and 44216299768 (not in
   44224758109, which failed in connection): nothing or little delivered for 3-4 s on the shared
   connection ("0.0 MiB, longest gap 4044 ms"), then stop() takes 2-6 s. The test is
   test/transport/groups_test.cc `upload`, outside this quest's file set (`groups_upload*`).

## Next

1. Read the five jobs; confirm or drop each hypothesis; commit the fix for cause 1 and run it as a
   job with the same forcing (expect pass), plus `--fails-without` where useful.
2. Decide causes 2 and 3 from the printed sndbuf/lowat numbers and CI's macos-x86_64.
3. Cause 4: find whether it is product (host transport) before touching it (A3).

## Blocked on

Nothing yet. groups_test.cc is outside the file set; will ask before editing it.

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
