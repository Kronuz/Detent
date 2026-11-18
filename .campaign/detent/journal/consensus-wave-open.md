# consensus-wave-open: journal

State: waiting on jobs 261118-002456-3fbf8b
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

## Next

1. Read probe 3fbf8b (64, 256, cut at the new pin).
2. A1: old-pin job (`--fails-without`), x20 under 4-way load at 799525d+. A2: suite plus mutant
   job (both worker.h checks removed, `--fails-without`). A3: TSan job. Check CI on the push.

## Blocked on

Nothing yet.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

Not yet.

## Rulings I made

- The wave test uses the Driver on an inline scheduler with InlineCompletion (driver_test's
  shape), lone voters, and the same `turn_options` as the other tests and strata's footprint
  test, so it reproduces the Xapiand rows without strata.
