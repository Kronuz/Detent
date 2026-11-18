# consensus-wave-open: journal

State: waiting on jobs 261117-191119-28bc6a 261117-191129-a23d83
Quest: .campaign/detent/quests/consensus-wave-open.md (the lead's; never edit it)

## Doing

Reproducing on consensus with `test/wave_open_test.cc` (consensus:d668ae2): 64 lone voters
opened, created, proposed and applied in one wave on one Host loop, turn log 16 KiB segments
of 1 KiB blocks (the fixtures' and strata's footprint test's `turn_options`). The test prints
the turn driver's failure. Probes: 28bc6a (consensus's journal pin 7c08540; waves 64, 8, 64/8,
32, 16) and a23d83 (strata's journal pin cc95ba6). Branch pushed; GitHub CI run 44229213026
also runs `consensus_wave_open 64` on every platform.

Hypothesis (one): `TurnDriver::start()` (journal, include/journal/turn_driver.h) puts every
pending record in one turn with no segment bound; 64 groups' create records (checkpoint record
+ initial batch, ~65 B framing each plus ~12% block overhead) exceed a 16 KiB segment, so
`TurnLog::encode` throws "turn exceeds a segment", the driver fails every staged operation, each
group's Journal sets `owner_->failed`, and the Worker's next `run_one`/`submit` finds the store
fenced with `failed_` unset: "storage fenced outside worker" (worker.h:467, 671). The check is
right; it hides the turn's error. If confirmed, the cause is journal's: stop and report.

## Next

1. Read the probes; if the turn driver fails with "turn exceeds a segment", report it as
   journal's (Blocked on) with the fix options.

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
