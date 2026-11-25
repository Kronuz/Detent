# journal-close-in-doubt review: correctness

Commit: journal:6ab0fd2a9d89d6618ac46f6968b30e6e9cfce5ff
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-145734-c35afd (close_in_doubt at the fix) and 261119-145814-f4c7ce (`--fails-without`:
  close_in_doubt and failed_close_crash fail under main's host.h): A1 is shown for a new Store.
- 261119-145740-13f24c (failed_close_crash, 192 points: each op of the close's commit, before and
  after its effect, x a later create in the same or another slot, x 8 crash seeds): A2.
- 261119-145750-f2f69d, 261119-145754-5345d7 (mutants close-restores-open, hold-ignores-doubt): A3.
- 261119-145820-931e5c (suite), 261119-145900-52089d (host + shared_store under TSan), CI: A4.

These settle the host side. I did not rerun them.

## What I checked myself

Read the diff (`include/journal/host.h`, `test/host_test.cc`, `test/mutants.py`,
`docs/design/host-layout.md`) against `journal.h`, `store.h`, `turn_driver.h`,
`docs/mechanisms/group-lifetime.md`, consensus's `worker.h` and `driver.h`, and Xapiand's
`quests/group-core.md`.

1. Q1: "any failure means in doubt" is sound. The in-doubt entry stays open in memory with its old
   `base`/`manifest` (`update`'s restore), and `turns.forget` never runs (host.h:429-430). So the
   log keeps every record through the close's checkpoint `S`. `retired_floor` reads the resident
   `base`, so retention keeps them too. The disk ends in one of three states:
   1. Closed at `S`: it covers every acknowledged record, because `close_group` demands
      `sequence >= turns.sequence(group)` and nothing staged.
   2. Open at the old base: the log replays through `S`.
   3. Rewritten open by a later commit of the slot: `commit` drops the stored closed entry because
      `shards_[slot].contains(id)` (host.h:513). This is state 2.

   None of them loses a record, provided nothing appends after the failed close. Durably open
   while the host treats the group closed cannot come from close_group: the host never treats a
   failed close as closed. The only closed-in-memory-after-failure case is a failed reopen, which
   predates this quest, and it is harmless because no record follows it.
2. Q2: create and recover each take `hold()` first (journal.h:571, 591), so `create_group`,
   `reopen_group` and `recover_shared` all sit behind the EIO gate. `held()` also refuses
   `next_artifact` and `reopen_group`. Only a commit or the host's open iterates `shards_`
   (`commit`, `known`, the lost marking). `hold()` reads `shards_` on the group's loop, which is
   the same discipline as `settle`/`staged` and the commits. A second thread's `hold()` cannot pass
   in the window before `in_doubt` is set: the closer keeps its hold until its Journal ends (the
   catch path skips `hold_.reset()`, journal.h:361-363). The gate leaves one path uncovered, the
   closing Store itself; see F1.
3. Q3: consensus never calls a shared `Store::close`. Its `Worker::close` only sets `closing_`, and
   the worker calls only `store_.create` and `store_.recover` (worker.h:140, 167). EIO cannot reach
   consensus. Xapiand group-core records the contract (`quests/group-core.md:107`, msg-144).
   `host-layout.md` states it. `group-lifetime.md`, `Host::hold`'s comment and the CHANGELOG do
   not (F2).
4. Q4: the tests check values (`errc::io_error`, `bundle`, `batches == acknowledged`), and the
   sweep fails at main. One plausible wrong change passes the whole suite (F1). Probe:
   `.scratch/detent/review-journal-close-in-doubt-correctness/fence_probe.py` (`fix` | `mutant`).
   It adds a case `review_fenced` to host_test: a sync-failed close, then an append through the
   same Store, a create in another slot, and a power cut at 8 seeds. It runs every host case plus
   journal_test and shared_store_test.
   1. 261119-153332-860a8c, at the fix: everything passes, and the failed Store refuses the append.
   2. 261119-153334-304c12, with `owner_->failed = true` removed from `Journal::close`'s catch
      (journal.h:362): every existing host case, journal_test and shared_store_test pass. Only
      `review_fenced` fails. The failed Store appends, and at all 8 seeds the reopened host says
      "the turn log lost this group".

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | The doubt is safe only because the closing Store is fenced (ruling 5: `Journal::close`'s catch sets `owner_->failed`, journal.h:362), and no test checks it. Dropping that fence lets the failed Store keep appending past a close the disk may hold, which is the quest's own loss: the group is lost at 8 of 8 seeds. The whole suite still passes, so A1's "never appendable" has an unproven path, and a refactor of `Journal::close` would bring the bug back silently. | 261119-153334-304c12 (mutant: suite green, `review_fenced` lost at every seed); 261119-153332-860a8c (passes at the fix) | In `close_in_doubt` (or the sweep), append through the same Store after the failed close and check that it throws. Add a `mutants.py` entry `close-unfenced` (the journal.h:362 catch without `owner_->failed = true`) killed by that case. `fence_probe.py`'s `review_fenced` is a ready model. |
| F2 | no | gap | The EIO contract is stated only in `docs/design/host-layout.md`. Three places still describe only `EWOULDBLOCK`: `docs/mechanisms/group-lifetime.md` rule 1 (whose header names consensus and Xapiand's pool as its readers) and its "Closing" paragraph, `Host::hold`'s comment (host.h:347-350), and CHANGELOG.md, which has no entry for this consumer-visible change. | `group-lifetime.md:14-17, 58-66`; host.h:347-350; CHANGELOG.md | Add one line each: a failed close leaves the group in doubt, and create/recover throw `std::system_error(EIO)` until the Host reopens. |
| F3 | no | nit | Ruling 2 (owner's call). A close that provably wrote nothing, such as ENOSPC at the pending shard's create (`failed_close`), now takes the group offline until the whole Host is rebuilt. Before this change it stayed usable. That narrows availability. | host.h:425-429; host_test.cc `failed_close` | Options: keep it (simplest; restart resolves it), mark doubt only once the replace has started, or resolve live on a later successful commit of the slot (ruling 4; sound, since that commit durably writes the group open). |
| F4 | no | nit | An EIO-refused `hold()` returns before `settle(at)`, so that call does not settle the loop's released groups. This is harmless, because the next hold or the loop's own settle does it. | host.h:364-367 | None needed. Move the check after `settle(at)` if wanted. |

## Not run

- No independent crash model or power-cut sweep beyond `review_fenced`'s 8 seeds. A2 rests on
  13f24c.
- No TSan run of my own, and no Linux runs (CI's).
- The `TurnDriver::forget` throw after a durable close (host.h:430), which can only be a
  `bad_alloc`, is out of scope and not probed.
- I did not check Xapiand group-core's handling of the EIO, only that its quest file records the
  contract.
