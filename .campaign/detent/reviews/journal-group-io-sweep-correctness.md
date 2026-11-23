# journal-group-io-sweep review: correctness

Commit: journal:965cd553fd639e50794a2fb6cec7cdd53da5e888
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The mechanism is correct: no live view is dropped or left unpublished, the table stays within
max(2 x live at the last sweep, 64), and the test and both mutants check values. One blocking gap:
A4's doc is not at this commit, and the doc there still describes the old O(live) sweep as current.

## Evidence I relied on

- 261118-123519-0b53cd: suite at journal:965cd55, exit 0. This covers A1 (`host_test view_sweep`).
- 261118-123533-1944ae: the never-sweeps mutant (journal:5522996, which deletes only the `erase_if`
  line). It fails the bound checks with 4100 and 4500 entries against a bound of 200 and a floor
  of 64.
- 261118-123541-c1f81b: the sweeps-a-live-range mutant (journal:6be5c51, predicate `true`). It
  fails "a held view lists its group's first artifact" for all 200 held views. Each mutant changes
  one line and fails the check for its own claim, so A2 is shown. `campaign status` still lists
  A2 as pending, which looks like the jobs' tagging (F5).
- 261118-123558-25b19c: exclusive A/B. Random ids at 1M: 132,537,331 ns before, 667 ns after.
  The 1K median is 361 ns after, so 1M/1K = 1.85x, inside A3's 2x. Each 1M run makes 2,000,000
  calls, which spans the amortized sweeps.

## What I checked myself

The probe scripts are in `.scratch/detent/review-journal-group-io-sweep-correctness/`:

- `probe.cc` wraps each arm's own `host_test.cc` and adds the `closed_view`, `lone_view`, `race`
  and `control` cases.
- `build.sh` builds TSan `journal_host_test` for base (journal:ea397ab) and head.
- `run.sh` and `run-history.sh` run the cases.

The jobs:

- 261118-140644-548f5e: `closed_view` and `lone_view` on both arms, plus head's whole host_test
  under TSan (811 checks, 0 failures, exit 0).
- 261118-144200-9368a8: the same, plus a TSan control. It reports `thread_sanitizer: on`, links
  `libclang_rt.tsan_osx_dynamic.dylib`, and the control race reports, so the builds are
  instrumented. Head's host_test under TSan: 811 checks, 0 failures.
- 261118-144636-e96deb: the `race` probe on both arms at TSan `history_size` 2 and 7.

By reading the code (`include/journal/host.h`):

- Threads (Q1). Callers of `group_io` (host.h:294):
  - `Journal(Host&, group)` (journal.h:107), on the Store's thread, which is its group's loop.
  - Callers of `publish` (host.h:490): `commit` on the loop that runs a table transition, and
    `mark_lost` at open.
  - Readers of `Range`: the scan closure, on whichever thread scans. It copies
    incarnation/mark/high under `range->mutex` (host.h:308), and `publish` writes them under the
    same mutex (host.h:493). One Range shared by several views adds readers of one
    mutex-protected snapshot. It adds no unsynchronized access to the Range.
- No stale range (Q1 and Q3). A held view sees the same values at base and head at every step of
  `closed_view`: open, closed, reopened and after a new checkpoint (2, 2, 2, 4 artifacts on both
  arms). `publish` reaches the one shared Range wherever base reached each per-view Range. Within
  one host lifetime the incarnation changes only through `create_group`, which needs status
  `none`. Both arms publish a new incarnation to every live view of the group.
- Live views are never dropped (Q2). The sweep predicate is `weak_ptr::expired()`. `ranges_[group]`
  replaces a slot only when `lock()` fails, and `publish` updates the slot's live Range. The
  sweeps-a-live-range mutant proves the test catches a violation.
- Bound and cost (Q2). Size is at most `sweep_at_` = max(2 x live at the last sweep, 64). The
  check runs before the insert. After a sweep that leaves L entries, at least `sweep_at_` - L >=
  `sweep_at_`/2 inserts pass before the next sweep, so each insert pays at most 2 entry visits.
  "2 x live + 64" holds only against the live count at the last sweep (F4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | A4 is unmet at the reviewed commit. `docs/perf/group-io.md` has no after table, and it still says in the present tense that "Every call runs `std::erase_if(ranges_, expired)`" and is O(live ids). After this lands, that is false. `campaign status` marks A4 met only from the journal row, which says "pending A3's numbers". | `git show journal:965cd55:docs/perf/group-io.md`; diffstat of `origin/main..965cd55` (bench, host.h, host_test only) | Commit the after table from 261118-123558-25b19c: medians and calls/run, before beside after, with 1M/1K = 1.85x. Rewrite "What a call does" and "Consequence" for the batched sweep and the shared Range. |
| F2 | no | defect | Behavior change, contrary to Ruling 2's "behavior-identical". If a group is closed while one of its views lives, a new `group_io` of it shares the old Range. That new view lists the closed incarnation's artifacts: 2 at head, 0 at base. Base gave each new view a zeroed Range when the closed entry had left memory (`resident` null). The arms agree once `reopen_group` publishes, and they agree when no view is held (`lone_view`: 0 on both). No in-tree reader scans in that window: `recover_shared` reopens before any read, and the Store builds its inventory after recover. Fixing it would narrow what the view shows, so it is the owner's call. | 261118-140644-548f5e, line "fresh, closed, held lives" (base 0, head 2); host.h:300-305 | Options: 1. Keep it; the new view then agrees with the held one, which is arguably more correct. Correct Ruling 2 and the `group_io` comment, and pin the behavior with a `closed_view` case. 2. Restore base exactly by giving a view of a non-resident group its own Range; this brings back per-view ranges, the multimap and the walk in `publish`. I recommend option 1. |
| F3 | no | defect | Pre-existing data race, not introduced here. `group_io` mutates `ranges_` on one loop's thread while another loop's `commit` -> `publish` reads it, with no lock. TSan reports it at base (2 reports: `erase_if`/`emplace` at host.h:294 against `publish`) and at head (20 and 21 reports: `ranges_[group]` insert and sweep at host.h:297-300 against `publish` at host.h:491). Head reports more because it has more distinct racing addresses; the race shape is unchanged. Concurrent loops are supported (`loop_slots` runs `create_group` from two threads). | 261118-144636-e96deb (base history 2: 2, history 7: 2; head: 20, 21); 261118-144200-9368a8 (base 0 at the default history, head 20) | Out of this quest's scope. File a quest to guard `ranges_` (a mutex taken by `group_io` and `publish`) or confine both to one thread. Its test is `probe.cc`'s `race` case under TSan. |
| F4 | no | nit | The bound is relative to the live count at the last sweep, not the current one. After many views die, nothing is released until enough new ids reach `sweep_at_`. Groups that reopen reuse their slots and add nothing. Up to 2 x the peak live count of map nodes plus weak-held `make_shared` blocks (about 176 B each, per the doc's sizing) can stay resident indefinitely. Base released them at the next call. | host.h:296-299; reasoning, not measured | State this in the doc (F1). Optionally also sweep when `sweep_at_` is far above twice a recent size; this is not required. |
| F5 | no | nit | `campaign status` lists A2 as pending although both mutant jobs ended as `--fails-without` requires. Their specs show `fails_without: 2` but no `--for A2` tag at the quest head. | `campaign status journal-group-io-sweep`; job specs | Retag the mutants or record A2 by job id in the journal's Evidence table. |

## Not run

- CI (GitHub run 44392889900) and the Linux and sanitizer matrices: CI's job, and still running.
- TSan of the whole suite. Only `journal_host_test` ran under TSan, at head (811 checks, 0
  failures).
- Memory retention after mass view death (F4). It is reasoned, not measured.
- No benchmark rerun: A3 rests on 261118-123558-25b19c.
