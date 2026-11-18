# consensus-close-fenced review: correctness

Commit: consensus:c5d15d9fe1551aaba53ff4b442e8062d9e432b6c
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The Driver change is sound: nothing holds a `Group` after pump erases it, and the erase is safe for
the ready list and `groups_`. Neither blocking finding is in driver.h. One is a stale contract doc.
The other is that a created row, the row Xapiand counts, keeps 2,398 B in the journal after close
and purge.

## Evidence I relied on

- 261118-000951-aad80b (plain) and 261118-000959-776077 (ASan), at c5d15d9: 46 of 46 close checks,
  with "3658 bytes held open, 178 left after close before the Host's purge, 10 after" per row.
- 261118-001006-e17e93 (TSan, A4) and 261118-001042-9c4249 (suite, A5), at c5d15d9: passed.
- 261118-001028-7ef567 (settle's `if (g.closed) return;` dropped): heap-use-after-free under ASan,
  exit 134. 261118-001036-648cac (pump's erase dropped): `bytes` throws "the group is open on this
  Driver", exit 134. So both changes are load-bearing.
- CI at c5d15d9: only the macos-x86_64 known flakes fail, as the lead reports. I did not rerun CI.

## What I checked myself

Scripts are under `.scratch/review-consensus-close-fenced-correctness/` in the home checkout:
`probe.sh` builds `review_probe.cc`, which includes `driver_close_test.cc` for its Loop and
counting allocator. `mutant_unqueued.sh` is a mutant run.

- 261118-010529-7321fa (`probe.sh`, plain Debug, c5d15d9). Results:
  1. In the same Host, recover after a clean close and after a fenced close both throw "the group
     was recovered on this host; reopen the host to recover it again".
  2. In the same Host, create after either close throws "the group exists, is closed, or is lost".
  3. In each of those cases the row closes again (`groups() == 0`). That covers A2's
     create/recover-threw path, which the suite did not test.
  4. After the Host reopens on the same disk, recover succeeds for both the clean and the fenced
     close.
  5. Opened-only rows, 10,000 of them, hold 178 B per row after close. One bare `Host::group_io`
     call, with no Driver open, brings that to 10 B per row, so the 178 B is the Host's `ranges_`.
  6. Created rows, 2,000 of them (`lone` then close), hold 8,286 B per row while open. After close
     they hold 2,566 B per row, and 2,398 B per row after the purge, with `groups() == 0`.
- 261118-010608-89bed0 (`mutant_unqueued.sh`, ASan+UBSan Debug, c5d15d9): erase never unlinks a
  queued group (`if (g.queued)` becomes `if (false)`). driver_close_test, driver_test and waiter_test
  all pass (46, 124 and 129,439 runs, 0 failures), so the mutant survives. No test erases a group
  that is still queued.
- Read every `&g` capture and every re-entrant path in include/consensus/driver.h (Q1, Q2 below).

### Q1: can anything hold `Group&` or `Group*` after the erase?

No.

1. `launch()` (driver.h:923-935) and `watch()` (driver.h:938-944) capture `&g`. They count in
   `g.io`, and `close_step` sets `closed` only at `!g.io && !g.watching` (driver.h:916). Each
   callback finishes, including its `mark_ready`, before the step that closes the group.
2. Timer: `close_step` disarms before `closed` (driver.h:917). Nothing re-arms after `closed`,
   because step returns at driver.h:647 and :657. The wheel's `cancel` unlinks synchronously on the
   owner thread (wheel.hh `cancel`). `arm` is owner-only here, so no inbox entry is left behind.
3. Waiters: `closed` requires no linked waiter except the close calls (`others`, driver.h:906-912),
   and those are unlinked by `complete`. A token cancel whose hop is pending finds `owner == nullptr`
   (`Cancel::hop`). `~Call` unlinks only when `owner` is set. `Call::group` is used only
   synchronously, in create and recover.
4. `complete` posts and never resumes inline (`inline_scheduler::post` queues). A close caller
   therefore resumes after pump has erased the row, and its `open(id)` succeeds.
5. `close_step` is reached only from `step` (directly, through `settle`, or through `fail_all`), and
   `step` is called only from `pump` (driver.h:608). Every caller up the stack returns on `closed`:
   settle at :842 (the fix), step at :647 and :657. A row with a null `worker` therefore never
   outlives pump, and `state()` dereferencing `worker` unconditionally (driver.h:235) is safe.

### Q2: is erasing during pump's iteration safe?

Yes.

1. Pump pops the head and holds no list cursor. `erase` scans for the predecessor only when the
   group was queued again in its own step, and it fixes `ready_tail_` (driver.h:614-621).
2. `groups_` is erased by key, with no iterator live across `step`. A Sink's re-entrant `open` can
   rehash the map, but `Group` lives behind a `unique_ptr`, so `g` stays valid.
3. The queued branch is correct by reading, but no test reaches it (N3).

### Q3: recover after close

The contract is honest but wider than the test comment says. A Driver close never closes the
group in the journal table, so any id this Host created or recovered refuses both recover and
create until the Host reopens, after a clean close as much as after a fenced one (probe 7321fa).
journal.h:578-580 at journal:7c08540 documents this, and recover works after a Host reopen.

Xapiand's D115 plan works: close the fenced row, then drop it. But hunt.md's "a test where reopen
succeeds" only holds as `Driver::open`, or as group open after a Host reopen. An in-process
`strata::group::open()` that recovers the same id will throw Corruption (N1).

### Q4: the 178 B

It is not a leak. It is lazily reclaimed and bounded by the rows closed since the Host's last
`group_io`. Any later open of any group on that Host reclaims it (probe: 178 to 10 B per row on a
bare `group_io`), and so does destroying the Host. A caller that closes N rows and never opens again
holds 178·N B until the Host goes, which is about 5% of an opened-only row. The real retention is
the created row's 2,398 B (F2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | docs/DRIVER.md still describes the old row lifetime. Line 135 says `close` "frees the group's state; its table entry stays, so late plain calls are dropped and counted". Lines 83 and 91 tie `dropped(id)` to a closed group. After this change the row is erased, and a late call on a closed id counts in `dropped_unknown_`: `dropped(id)` for a closed id returns the Driver-wide unknown count, not the row's own. driver_test.cc:458 and waiter_test.cc:302-304 pass only because nothing else counts as unknown. Xapiand's group-core consumes this contract (consensus:driver). | docs/DRIVER.md:83, :91, :135; driver.h:228-231, :519-522 | Lead adds docs/DRIVER.md to the file set. Rewrite the three lines: close erases the row and the id opens again on this Driver, and a closed id's late calls count with unknown ids. The per-row count lost after close is the owner's call: keep it (document it, my recommendation) or keep a closed-row count elsewhere. |
| F2 | yes | gap | A3 is proven only for rows that never create. A created row, the case behind Xapiand's "about 6.8 KB per row", keeps 2,398 B of its 8,286 B after close and the Host purge, with `groups() == 0`. The Driver frees its own part; the rest stays in the journal (turn log, table or Host state) until the Host reopens. The quest's outcome "its bytes return" is therefore unproven for created rows, and Xapiand's D115 check ("measure at 10^4 that a closed-then-dropped row returns its bytes") would fail. | 261118-010529-7321fa: `created rows=2000 groups()=0: per row open 8286, after close 2566, after group_io purge 2398` | Extend `bytes` with a created-row pass that prints the per-row figure (a report, not a failing check), and state it in the journal's Result. The lead records a decision: accept for this quest and open a journal follow-up (attribute the 2.4 KB, which may mean a Host close_group on Driver close), or widen this quest. |
| N1 | no | gap | Recover or create of an id this Host already created or recovered is refused after any Driver close, clean or fenced, until the Host reopens. The test comment and Ruling 4 frame this as fenced-only, and the quest's Contract ("after it completes the id is free") omits it. | 261118-010529-7321fa lines 1-6; journal.h:578-588 at journal:7c08540 | Qualify the Contract and the close doc: the id is free on the Driver; create or recover of it needs the Host reopened. Tell the Xapiand lead before group-core writes its "reopen succeeds" test. |
| N2 | no | nit | The 178 B per opened row is the Host's lazy `ranges_` purge (`group_io`, journal host.h:284). It is bounded, not a leak (Q4). Separately, that purge scans every range on every `group_io`, which is O(n) per open (journal's, outside this quest). | 261118-010529-7321fa: 178 to 10 B on a bare `group_io` | None here; a journal note if the lead wants one. |
| N3 | no | gap | No test erases a group that is still on the ready list. With the unlink removed, three ASan suites pass. The branch reads correct, but a mistake there would be a use-after-free. | 261118-010608-89bed0: `mutant killed: 0` | Add a case that queues the group again in its closing step (Ruling 2's witness apply, or an inline IO completion in the same step), then rerun `mutant_unqueued.sh`; it should fail. |
| N4 | no | nit | The comment on `fenced_in_flight` says "a Sink that throws mid-batch", but the case fences through `application_failed`. Its `check(proposal->done)` reads neither the value nor the error. | test/driver_close_test.cc:222-235 | Fix the comment and check the proposal's outcome. |
| N5 | no | nit | consensus:b7f48b2 changes tools/mutants.sh, which is outside the quest's file set (driver.h, test/driver_close_test.cc, CMakeLists.txt). | `git diff origin/main...HEAD --stat` | Lead widens the file set, or the commit moves to its own change. |

## Not run

- Did not run TSan or the suite myself; I accepted e17e93 and 9c4249.
- Did not rerun or inspect CI logs beyond the lead's summary.
- Did not find where the created row's 2,398 B live inside the journal (F2 asks for that).
- Did not test a `strata::group::open()` path or anything else in Xapiand.
