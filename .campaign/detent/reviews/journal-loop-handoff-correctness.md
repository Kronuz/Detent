# journal-loop-handoff review: correctness

Commit: journal:87d4382e6f4b65914317d9c889921e63a11b56ab
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261120-181331-7a0bfd: Debug host_test at 87d4382, loop_handoff 7/0, handoff_close 4/0, full 1028/0.
- 261120-172830-714b37, 261120-181334-f52e5a: handoff_close fails (exit 134) with adopt_loop a no-op.
- CI run 44854682137 at 87d4382, every job green. The linux-tsan job (136693983606) builds
  RelWithDebInfo with `-O1 -g -fsanitize=thread` and no `-DNDEBUG` (cpp-libraries
  `.github/workflows/cmake.yml:265`), so the owner asserts and the forked abort checks are live
  there; it ran journal_host (Passed, 1.89 s).

## What I checked myself

Scripts in `.scratch/detent/review-journal-loop-handoff-correctness/` (home checkout):
`mutate.py` applies a mutant or the stress case to a copy of `$JOB_SRC`; `handoff_stress.cc` is a
2-loop case with two workers per loop contending under the loop's mutex, each calling
`adopt_loop` before create/close/reopen/group_io, listing held views outside the lock, checked
against a sequential model of each group's status.

- 261120-224742-6be464 (`mutants.sh`, Debug). Baseline: loop_handoff 7/0, handoff_close 4/0,
  handoff_stress 600 ops, ~400 owner switches, 0 mismatches. Mutants of `adopt_loop`/`owned_ranges`:

  | Mutant | loop_handoff | handoff_close | handoff_stress (probe) |
  |---|---|---|---|
  | `clear`: adopt sets owner to `thread::id{}` (next caller binds) | survives | survives | survives |
  | `every_loop`: adopt sets every loop's owner | survives | survives | killed (abort) |
  | `loop_zero`: adopt always sets loop 0's owner | survives | survives | killed (abort) |
  | `no_check`: assert removed | killed (3 failures) | killed (1) | survives |

- 261120-224744-26c7b7 (`tsan.sh`, Debug + `-fsanitize=thread`, macOS): handoff_stress three runs
  (391, 489, 497 owner switches), loop_handoff, handoff_close, loop_views: 0 TSan reports, 0 failures.

Code read: every range-table access goes through `owned_ranges` (include/journal/host.h:539), reached
only from `group_io` (:317) and `publish` (:533); `publish` only from `commit` (:529), from
`update` (:506), from `create_group` (:409), `reopen_group` (:417), `close_group` (:428),
`next_artifact` (:443), plus `mark_lost` at open while `ranges_` is empty (:620). The host's only
callback handed to a driver is `retire_with` -> `retired_floor` (:614, :470), which reads shards,
not ranges. strata's `turn_sealer` makes all table calls under `turn_mu_`
(strata include/strata/core/turn_seal.hh:109-122).

Answers to the lead's questions:

1. Old owner after the handoff. The host posts nothing: no timer, completion or driver callback
   reaches the table, so only the caller's own calls can. A call outside the caller's
   serialization is a contract violation; in debug it races on `Ranges::owner` itself (a plain
   `std::thread::id`, host.h:220), which TSan reports, but the assert will not reliably catch it.
   CI TSan covers the tests' handoffs, which are ordered by `thread::join`, and loop_views; no
   suite test hands a table back and forth under a mutex. The probe stress does, and is clean.
2. Thread-id reuse. Only the debug check compares ids; release keeps no owner. Reuse can make the
   check miss a stranger, never abort the owner (docs rule 5 says so). Nothing relies on unique ids.
3. Handoff during close and during a turn. `adopt_loop` writes only the debug owner field after a
   bounds check (host.h:290-295); it touches no driver, shard or waiter, so it cannot lose a
   completion or close twice. Under strata's `turn_mu_` it cannot interleave with a turn or a close.
   handoff_close checks the status is `closed` after one close; the aborted close runs in a forked
   child, so the parent's group is untouched.
4. Values and mutants: see F1 and F2.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | Three mutants of the handoff survive both quest tests. `clear` violates A1 directly: after `adopt_loop` on thread B with no call from B yet, the old owner's call binds and passes instead of aborting. `every_loop` and `loop_zero` ignore which loop is handed over; the tests use a 1-loop Node, so the `loop` argument is checked only for its bounds. | job 261120-224742-6be464; test/host_test.cc loop_handoff, handoff_close (`Node node(disk)`, 1 loop) | Add to loop_handoff (or a new case): a thread adopts and exits with no table call, then the old owner's view aborts (kills `clear`); on a 2-loop Node, a thread adopts loop 1 only, then the first thread's loop-0 call passes and its loop-1 call aborts (kills both loop mutants). Rerun `mutants.sh`: all four mutants killed. |
| F2 | no | gap | No suite test exercises the design's named interleaving, alternating owners under a caller mutex, under TSan with two workers per loop. Probe handoff_stress does it cleanly (TSan, ~400 switches per run). | job 261120-224744-26c7b7; `handoff_stress.cc` | Optionally adopt `handoff_stress.cc` as a host_test case; it also kills `every_loop` and `loop_zero`. |
| F3 | no | nit | Weak value checks: `host.status(2) != none` and `status(3) != none` would also accept `lost` or `closed`; `stored` is the expected value. The "aborted close left this process's group open" check can never fail, since the abort ran in a child. | test/host_test.cc loop_handoff, handoff_close | Compare `== j::GroupStatus::stored`. |

## Not run

- Linux or gcc runs of the probes (CI's TSan job covers the suite's tests only, not the probe).
- strata with `adopt_loop` adopted; strata:6421d4b3's workaround is still in place.
- Consensus's coroutine loops (`next_artifact` from Store artifact preparation): unchanged by this quest.
