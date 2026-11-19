# journal-close-in-doubt: journal

State: waiting on jobs 261119-155358-d7b542 261119-155359-aa394f 261119-155400-64866e 261119-155402-ef5a34 261119-155403-6701c3
Quest: .campaign/detent/quests/journal-close-in-doubt.md (the lead's; never edit it)

## Doing

Review fixes (F1, F2) at journal:a64c618, pushed; reruns queued: close_in_doubt d7b542 (A1), sweep
aa394f (A2), mutant close-unfenced 64866e (A3, --fails-without), TSan ef5a34 and suite 6701c3 (A4).
At journal:6ab0fd2 every earlier job passed (brief's table below).

## Next

1. When the reruns pass: `campaign accept journal-close-in-doubt correctness --confirm F1=261119-155400-64866e ...`
   is the lead's; paste the brief's table at a64c618, set State to ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-145734-c35afd (close_in_doubt at the fix); 261119-145814-f4c7ce (--fails-without: close_in_doubt and failed_close_crash both fail under main's host.h 0202ebc) |
| A2 | 261119-145740-13f24c (failed_close_crash, 192 points); 261119-145814-f4c7ce (at main's host.h) |
| A3 | 261119-145750-f2f69d (mutant close-restores-open), 261119-145754-5345d7 (mutant hold-ignores-doubt) |
| A4 | 261119-145820-931e5c (suite), 261119-145900-52089d (host + shared_store under TSan), CI 44633320764 (Linux TSan, ASan/UBSan) |

Local run before the jobs: under main's host.h, close_in_doubt fails 4 checks ("the turn log lost
this group" after the restart) and failed_close_crash loses records at 24 of 192 points (each a
create in another slot behind a commit that reached the rename); at the fix 0 of 192.

## Result

journal:6ab0fd2. A failed `Host::close_group` commit leaves the restored open entry flagged
`GroupEntry::in_doubt` (in memory only). `Host::hold` refuses it with EIO and `held()` refuses it,
so no Store creates, recovers or appends to the group until the host reopens and reads whichever
state the disk kept; both cover every acknowledged record, since the log never forgot the group.
Later commits of its slot write it open. Docs: docs/design/host-layout.md (Close).
Tests: test/host_test.cc close_in_doubt, failed_close_crash (every op of the close's commit, before
and after its effect, x a later create in the same or another slot, x 8 crash seeds), failed_close
extended; test/mutants.py close-restores-open, hold-ignores-doubt.

Contract change for consumers (xapiand:group-core consumes journal:driver; consensus): after a
failed `Store::close`, a recover or create of that id in the same Host throws
`std::system_error(EIO)` until the Host reopens.

## Rulings I made

1. Closed-in-doubt over failing the host: one flag on the entry and one gate (hold), against a
   failed-host state every entry point would need to check. Only the one group stops.
2. Any failure of the close's commit puts the group in doubt, including ones that provably wrote
   nothing (a full disk at the pending shard's create): telling them apart needs per-step state
   in commit for a rare path, and a restart resolves both.
3. The in-doubt entry stays open in memory, so later commits of its slot write it open, not
   closed: sound either way since the log never forgot the group, and open keeps update()'s rule
   that a successful commit drops closed entries from memory. status() still says stored.
4. No live resolution: a later successful commit of the slot does not clear the doubt (it would
   be sound, since the disk then durably says open, but the quest asks for restart resolution and
   that is the smaller change). Possible follow-up: hold() retries the slot's commit.
5. The gate is Host::hold (every create and recover takes one first); the failed Store is already
   fenced (Journal::close sets owner_->failed), so it appends nothing either. Pinned since the
   correctness review (F1) by close_in_doubt's same-Store append and mutant close-unfenced.
6. CHANGELOG.md is outside the brief's file set (include/journal/**, src/**, test/**, docs/**); the
   lead asked for its entry (review F2), so it is in this quest's commit: the lead's to widen.

## Review findings (correctness, CHANGES at journal:6ab0fd2)

- F1 (blocking): close_in_doubt appends through the same Store after the failed close and checks
  it throws; mutants.py close-unfenced (journal.h's close catch without `owner_->failed = true`)
  is killed by it.
- F2: EIO contract in docs/mechanisms/group-lifetime.md (rule 1 and Closing), Host::hold's
  comment, CHANGELOG.md 2026-11-19.
- F3: kept (a close that wrote nothing also stays in doubt until restart); the lead records it.
- F4: left, as the lead ruled.
