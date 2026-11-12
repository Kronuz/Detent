# logger-followups: journal

State: waiting on jobs 261112-153011-56511a 261112-153013-18f5d2 261112-153016-7eaa61 261112-153028-970fc1
Quest: .campaign/detent/quests/logger-followups.md (the lead's; never edit it)

## Doing

Code at logger:0379a75 (branch quest/detent/logger-followups); all four findings fixed, jobs queued.

## Next

1. `campaign wait` the four jobs; paste `campaign brief`'s table here; answer anything a job shows; ready for review.

## Blocked on

Not blocking review, the lead's call (A2 literal reading, "no file includes scheduler headers"):
bench/wheel_ab/c and d (the frozen A/B experiment arms, outside this quest's file set) include
scheduler.h, and bench/wheel_ab/d/CMakeLists.txt links scheduler::scheduler. The root project no
longer fetches the scheduler, so `bench/wheel_ab/run.sh build-d`, arm C's target and
`docs/perf/wheel-port.sh` (the A3 harness of logger-on-wheel) cannot be rebuilt. Done in-set: arm C's
target and its three ctest rows are gone from CMakeLists.txt; the A and B library arms stay. Options:
1. Delete bench/wheel_ab/c, d, sizeof.cc, the run.sh arm bits and docs/perf/wheel-port.sh (the A/B
   is decided and recorded in docs/perf/wheel-ab.md and wheel-port.md). Recommended: nothing then
   links or includes the scheduler anywhere.
2. Keep them and let d/CMakeLists.txt fetch the scheduler itself (the arms remain reproducible).
Either needs bench/** and docs/** in the file set. Also out of the set and stale: README.md:81 and
AGENTS.md (lines 24, 45, 73, 105 name the scheduler and test/frozen) need a one-line edit each.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.

| Criterion | Evidence |
|---|---|
| A1 | 261112-153013-18f5d2 (pending) |
| A2 | 261112-153011-56511a (suite), 261112-153028-970fc1 (grep) (pending) |
| A3 | 261112-153016-7eaa61 (mutant, fails without), 261112-153011-56511a (suite incl. logger_deferred_ties) (pending) |
| A4 | `ARCHITECTURE.md`: "so the batch rarely takes an insert" |

## Result

F1: the batch key's call time is the record's `atom_created_at` in both places that build it
(`key_of`), and the old `seq` tie-break is back as a 32-bit `wheel_seq` in a padding hole of
`Logging` (sizeof(Logging) is 192 before and after, checked by compiling both logger.h). `test/deferred.cc ties`
(ctest `logger_deferred_ties`) logs 20 rounds of 200 lines with one due time and one call time through a
test-only friend (`logger_detail::TestAccess`) and requires logging order; `test/deferred_mutant.sh`
gains a `ties` mutant (tie-break returns false), killed locally ("CHECK FAILED ... pos[t][i] < pos[t][j]").
F2: `Line` has `cancelled` (clear() won), `unsure` (clear() lost: written at most once) and the
await waits for the required lines; `test/deferred_load.sh` repeats the deferred tests under one busy
loop per CPU. F4: ARCHITECTURE.md. F6: CMake fetches threadpool (63e03a5, the scheduler's own pin)
and links threadpool::threadpool; test/frozen is replaced by test/transcript.golden (33 KB, the
bytes the frozen implementation wrote; old == new checked before it was committed), compared by
test/transcript.cmake, so the transcript now runs under TSan too. CHANGELOG has the entry.

## Rulings I made

- Golden instead of the frozen copy: the frozen copy needs the scheduler. transcript.cc prints a
  very long line as head + length + FNV-1a (338 KB -> 33 KB) and replaces its call site's build-
  dependent path and line with `transcript.cc:LINE` (the line must equal the `__LINE__` it was
  logged at, so a wrong line still fails). Not run on Linux locally: CI is the check for
  cross-platform bytes.
- The ties test needs a call time of its own, which no public call gives (every call reads the
  clock): a `friend struct logger_detail::TestAccess` in logger.h, defined only in test/deferred.cc.
- `seq` is 32 bits and compared by signed difference: lines tying on (due, call time) are filed
  close together, so the wrap cannot misorder them.
- F2 fix is the "written at most once" form (race.cc's), not a bigger floor: a floor only moves the
  preemption window.
- A1's "suite 50 times under load" is the deferred tests (logger_deferred, _stress, _ties), the
  suite F2 concerns; the whole suite is 40 s a run.
- Arm C's CMake target and `logger_wheel_ab_check_C*` ctest rows removed (they link the scheduler).
