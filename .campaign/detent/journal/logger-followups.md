# logger-followups: journal

State: ready for review
Quest: .campaign/detent/quests/logger-followups.md (the lead's; never edit it)

## Doing

CI fixes at logger:2bafea1 (CI run 43335535307 green on every job). Transcript: the random section's
`std::string s(rng() % 60, 'a' + rng() % 26)` drew its two values in unspecified argument order (gcc
right to left, clang left to right), not a std distribution; now two sequenced statements in the
golden's order, so the golden is unchanged and serves every platform. deferred.cc: the lateness bounds
are opt-in (DEFERRED_LATENESS=1, `test/deferred_lateness.sh`), reported always; ordering, once-only and
never-early are the pass/fail path; the mutant script runs with the bound on (the late mutant needs it).

## Next

1. Review. Strict job 261112-162248-40c765 (lateness bounds, 20 repeats) is queued behind the
   machine; it passed at 2f300c9 (261112-160518-1a7ae1), same test code.

## Blocked on

Nothing. (The bench arms question was answered by the lead: delete. `logger_queue`'s exit crash is the
lead's new quest, logger-exit-crash; see Result.)

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261112-162247-3c16de (x50 under load, bounds off); TSan suite 261112-162247-219244; lateness bounds on a quiet machine 261112-162248-40c765 (queued; 261112-160518-1a7ae1 passed at 2f300c9) |
| A2 | 261112-161458-af4825, 261112-162248-78ca6c, 261112-162248-a68768 |
| A3 | 261112-161458-af4825; fails without: 261112-153648-ec9876, 261112-161449-b3e0b7 (the late arm needs DEFERRED_LATENESS=1) |
| A4 | `ARCHITECTURE.md`: "so the batch rarely takes an insert" |

A1's job runs logger_deferred, _stress and _ties 50 times each (150 passes) under one busy loop per
CPU, with lateness unbounded (a loaded machine has no bound; the bounds run only under DEFERRED_LATENESS=1). A3's `fails without` is
`test/deferred_mutant.sh`: the early and late arms and the new `ties` mutant are each killed.

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

Pre-existing, the lead's: `logger_queue`'s exit-without-finish test (the child's Finisher joins the LOG
thread for 200 ms, then statics are destroyed under it; the child dies with SIGSEGV in `Logging::emit`)
failed 5 times in a row in the 15:30-15:33 window (suite jobs 261112-153105-e27767, 261112-153300-08fa45
and local ctest), and 3 of 150 child runs on logger:67391e6 (main) at 15:35, so it is not this quest's
change; CPU load alone does not reproduce it (261112-153624-32e3d0 at main: 0 of 300 under a busy loop per
CPU; 261112-153632-87ee53 at the head: 0 of 300), so it looks like slow disk writes while other jobs build.
Not fixed here (a longer or unbounded join changes what exit does when a handler stalls): a decision for
the lead. The suite at the head passed (261112-153819-dedbc0).

## Rulings I made

- Lead's follow-up: bench/wheel_ab/c, d, sizeof.cc, docs/perf/wheel-port.sh and run.sh's c/d modes are
  deleted (ab.cc keeps backends A and B); wheel-ab.md says so in one line (sources at logger:dce2538).
  The A2 grep is whole-repo: no include, target or fetch anywhere, and no "scheduler" word outside
  docs/ and CHANGELOG.md. docs/perf/wheel-ab.md and docs/status/* keep the word where they record what
  was measured then ("the retired Kronuz/scheduler"); docs/ROADMAP.md is outside the file set and
  untouched (it names the scheduler in two historical notes).

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
