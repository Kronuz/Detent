# consensus-wave-open: journal

State: waiting on jobs 261118-002642-efda7e 261118-002706-6c6519 (queued) and CI run 44266992531
Quest: .campaign/detent/quests/consensus-wave-open.md (the lead's; never edit it)

## Doing

Cause confirmed and fixed in journal (D114, journal-turn-bound landed at journal:4272229).
consensus:799525d merges origin/main, repins journal 7c08540 -> 4272229 (CMakeLists.txt; task
and completion pins unchanged at journal's), and extends `test/wave_open_test.cc`:
- `consensus_wave_open_256`: 256 groups in one wave (more than one segment's turn).
- `consensus_wave_open_cut` (A2): after the wave commits, `disk.cut` loses power under the turn
  driver; every store fences outside its worker, and each worker must report "storage fenced
  outside worker" with a fenced role (without the check, Driver::settle reports
  "consensus group fenced" and the core is not fenced).

CI at d668ae2 (run 44229213026) failed on all 8 platforms in `consensus_wave_open` only:
"64 fenced; turn driver failed: turn exceeds a segment" at the old pin, the D114 cause.

Probe 3fbf8b at 799525d: 64 and 256 at once all commit, 0 fenced, turn driver healthy; cut at 64
fences all 64 outside their worker; at 128, the 64 in a later turn fence inside with "turn driver
failed" (journal turn_driver.h:230). consensus:c20f3fd accepts either, needs one outside fence,
and runs the cut ctest at 128.

Done at c20f3fd: cafc64 (A1) 4 concurrent loops x 20 runs of 64 + one 256 each: all
committed, 0 fenced, rc 0. 7a5cb2 (A1, fails-without) old pin: 64 fenced, "turn exceeds a
segment". ddd230 (A2, fails-without) mutant: 0 outside fences, first "consensus group fenced",
64 cores unfenced.

## Next

1. Read suite efda7e (A1,A2) and TSan 6c6519 (A3); CI 44266992531. Then fill Evidence and set
   "ready for review".

## Blocked on

Nothing yet.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-002646-cafc64 (x20 x4), 261118-002652-7a5cb2 (old pin fails); suite efda7e pending |
| A2 | 261118-002658-ddd230 (mutant fails); suite efda7e pending |
| A3 | 261118-002706-6c6519 pending |
| A4 | [manual] the Xapiand lead ran its reproducer, xapiand job ...003837-3e3d58 (strata:88d72629, consensus:c20f3fd, journal:4272229): `GROUP_FOOTPRINT_NOSYNC=1 GROUP_FOOTPRINT_WAVE=64 group_footprint_test 64 0` opened 64 at once, all committed their first write, 0 fenced on open, exit 0; all 64 fenced before the fix |

## Result

Not yet.

## Rulings I made

- A2's test is the wave test's `cut` mode: a power cut under the turn driver is a real store
  fence outside the worker. The mutant deletes both checks (worker.h:467, 671); without them
  Driver::settle reports "consensus group fenced" and the core never steps StorageFault.
- 4-way load for A1 is four concurrent loops of 20 runs of 64 groups (plus one 256 each).

- The wave test uses the Driver on an inline scheduler with InlineCompletion (driver_test's
  shape), lone voters, and the same `turn_options` as the other tests and strata's footprint
  test, so it reproduces the Xapiand rows without strata.
