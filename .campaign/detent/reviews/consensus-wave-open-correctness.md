# consensus-wave-open review: correctness

Commit: consensus:c20f3fd8cbd0b05d022acc253daabd2e2ca86967
Verdict: APPROVE
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The diff (`git diff origin/main...HEAD`) touches two files: the journal pin in CMakeLists.txt
(7c08540 to 4272229, journal-turn-bound, D114) and the new test/wave_open_test.cc with its three
ctest entries. It changes no line of include/consensus/worker.h. The two
"storage fenced outside worker" checks (worker.h:467 in `run_one`, worker.h:671 in
`submit_retained`) were already on main. The quest's fix is the repin alone, and the checks stay
as the quest file asks.

## Evidence I relied on

- 261118-002652-7a5cb2: at the old pin, the 64-group wave fences 64 of 64 with "storage fenced
  outside worker" and fails the turn driver with "turn exceeds a segment". This is the reported
  symptom, and the test fails without the fix.
- 261118-002646-cafc64: at c20f3fd, 64 groups and 256 groups each commit with 0 fenced. The
  job's 4 concurrent loops of 20 runs add no coverage (F5), but one passing run proves the fix.
- 261118-002658-ddd230: with both checks deleted, the cut test fails: 0 groups fenced outside
  their worker, and 64 cores are not fenced.
- 261118-002706-6c6519: TSan over the wave tests and the worker suites passes (A3).
- 261118-002642-efda7e: the suite at c20f3fd passes 60 of 61 tests. Only consensus_mutants
  fails, with "build failed" (see F3).
- CI run 44266992531 at c20f3fd is green on all 8 platforms, linux-tsan and linux-asan-ubsan
  included.
- A4 (261118-003837-3e3d58, the Xapiand reproducer) is the lead's to record; I did not rerun it.

## What I checked myself

The probe scripts are in `.scratch/review-consensus-wave-open-correctness/` in the home checkout.

- `single_check_mutants.sh 1` (261118-005853-b8f4c2): deletes only run_one's check
  (worker.h:467). The cut test fails at 64, 128 and 256 groups, so this site is load-bearing.
- `single_check_mutants.sh 2` (261118-005857-dab380): deletes only submit_retained's check
  (worker.h:671). The cut test still passes at 64, 128 and 256 groups (F2).
- `mutants_with_main_fix.sh` (261118-005900-81da52): applies main's one-line mutants.sh fix
  (consensus:b7f48b2) on top of c20f3fd. consensus_mutants passes in 794 s, so the suite's one
  failure comes from main, not from the repin.
- `cut_sweep.sh` + `wave_probe.cc` (261118-015255-916bf1) answers Q1's fence-during-open and
  fence-during-recovery cases. It cuts disk power at operation K during a create wave, and during
  a recover wave after a restart (a new Host on the same disk). It sweeps K from 0 to 2000 at
  64 groups and from 0 to 8000 at 256 groups, 1108 runs in all.
  - Without a cut, both recover waves pass: 64 and 256 groups recover and commit with 0 fenced.
    The quest's own test covers create only.
  - With a cut, every run holds the invariants:
    - the loop settles;
    - every call completes;
    - every fenced group's core is Fenced;
    - every group commits, fences, or has its create call report the failure.
  - Outcomes summed over runs:

    | Wave | Fenced outside the worker | Fenced inside ("turn driver failed") | Create call failed |
    |---|---|---|---|
    | create 64 | 7937 | 60 | 11971 |
    | create 256 | 928 | 1260 | 2164 |
    | recover 64 | 1344 | 0 | 0 |
    | recover 256 | 64 | 192 | 0 |

  The first sweep (261118-005849-ed1816) failed only because its invariant counted a create call
  that reports PowerLoss as stuck. That outcome is legitimate: the caller gets the error and no
  core exists. I relaxed the invariant and reran.

Answers to the lead's questions:

1. The checks are right.
   - `store_.fenced()` reads only the group's own `owner_->failed` (journal.h:114). A shared
     group's store is fenced outside its worker only when one of its own staged operations fails
     in the turn driver: a failed turn, or the new refusal of an oversize record. A store fenced
     outside can therefore never fence another, healthy group.
   - Every such failure resumes the group. Driver::watch and Driver::launch both call
     `mark_ready` (driver.h:911-932), so `run_one` runs the check at worker.h:467 and steps the
     core with StorageFault.
   - The worker's other entry points return early on `fenced()` without stepping the core. The
     next `run_one` still fences it, and the sweep found no fenced group with a live core.
   - Fence racing a close: `run_one` returns `retirement_turn()` before the check
     (worker.h:466). The close completes with the failed operation's error as its terminal
     reason, and the retiring core is never stepped. This behavior predates the quest.
2. The cut mode tests what it claims through run_one's check: deleting it fails the test (b8f4c2
   and ddd230). It does not cover submit_retained's check (F2). Its `outside > 0` assertion is
   weak, but `roles == groups` requires every core to be fenced, so no group can pass with a
   live core.
3. Nothing in consensus depends on the old turn behavior.
   - Consensus never calls `Journal::close`, `Host::close_group`, `TurnDriver::forget` or `seal`
     (grep over include/). So the new `close_group` guard, `group_closed` and the sealer changes
     never reach it.
   - `Driver::watch` ignores the exception type of a `durable()` failure.
   - The blocking entry commits while `!turns.idle()` (driver.h:1001-1016), so a wave that spans
     several turns drains there too.
   - `TurnDriver::run`'s new `post_call` requirement holds for the inline, blocking and asio
     schedulers. The suite and CI pass on all of them.
   - An oversize record used to fail the whole driver. Now it fails only its own group, a
     strictly narrower failure.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | nit | The handoff described the quest as adding the worker.h checks. The diff changes no line of worker.h: the checks are on main, and the fix is the journal repin alone. | `git diff origin/main...HEAD --stat`: CMakeLists.txt and test/wave_open_test.cc only | None. Describe the quest as "repin plus wave test" in the land note. |
| F2 | no | gap | The cut test kills only run_one's check. Deleting submit_retained's check (worker.h:671) leaves the cut test passing at 64, 128 and 256 groups. A2 still holds through worker.h:467. | 261118-005857-dab380 (survives), 261118-005853-b8f4c2 (killed) | Option: add a cut case that proposes to an idle group after its store is fenced (no record in flight), so that `submit_retained` is the first to see the fence. Option: accept, since run_one's check covers the outcome on the next turn. |
| F3 | no | gap | The suite at c20f3fd is not green: consensus_mutants fails with "build failed" because this branch's tools/mutants.sh does not compile driver_simulation.cc. Main fixed this in consensus:b7f48b2, and with that fix over this repin the test passes. | 261118-002642-efda7e; 261118-005900-81da52 (passes in 794 s) | Merge main (b7f48b2) into the branch and rerun the suite before landing. The land gate already requires this. |
| F4 | no | nit | CHANGELOG.md has no entry for the journal pin move to 4272229. Earlier pin moves are recorded (CHANGELOG.md:77). Consumers inherit new journal behavior: per-group oversize refusal, `group_closed`, the `close_group` guard, and `run()`'s `post_call` requirement. | CHANGELOG.md:9-78; journal CHANGELOG 2026-11-16 and 2026-11-17 | Add a one-line entry: "The journal pin moves to journal:4272229 (turn bound, D114)." |
| F5 | no | nit | A1's "x20 under 4-way load" adds no coverage. The wave test runs on `inline_scheduler`, `InlineCompletion` and `MemoryIO`, so it is deterministic and 80 runs equal one run. Only A4 (Xapiand on asio) exercises a real-thread wave. | test/wave_open_test.cc:113-116; 261118-002646-cafc64 | None needed. State in the evidence that the test is deterministic. |
| F6 | no | gap | Not probed: consensus does not check its largest storage record against the host's turn room. With the test fixtures (16 KiB segments, `rpc_bytes` 16384, `log_bytes` 64 KiB), a full batch might not fit one segment. Under 4272229 such a batch fences only its own group, with "storage fenced outside worker"; at 7c08540 it failed the whole driver. So this is not a regression. | include/consensus/worker.h:645-665 (`validate_configuration`); journal turn_log.h `turn_room` | Option, outside this quest: have `validate_configuration` refuse a configuration whose `storage_batch_size` exceeds `turn_room(segment_bytes, block_bytes)`. Option: keep it as is. |
| F7 | no | nit | The wave test prints "turn driver failed: unknown" in cut mode, because `jt::PowerLoss` is not a `std::exception` and `what()` falls through. | test/wave_open_test.cc:49-52; ddd230 log | Catch `jt::PowerLoss` in `what()`, as wave_probe.cc does. |

## Not run

- The Xapiand reproducer (A4). Its record is the Xapiand lead's job 261118-003837-3e3d58, which I
  did not rerun.
- No multi-threaded wave, with the turn driver on asio across threads. The wave test and my
  probes are single-threaded and deterministic.
- No check of whether consensus's largest batch fits a 16 KiB segment (F6).
- No sanitizer runs of my probes. TSan for the quest is covered by 6c6519 and CI.
