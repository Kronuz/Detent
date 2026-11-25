# journal-host-guards review: durability

Commit: journal:904037a0d77c37e6789a66b17a6c8e47b081a380
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Lens `durability`, carrying the lead's Q1-Q3. Read: the quest file, the journal (R1-R4),
`git diff origin/main...HEAD` in journal-wt, turn-seal's landing at journal:555b69f, the earlier
`journal-driver-durability.md`, and task:95798a0 `task.hh` / `task_asio.hh`.

## Evidence I relied on

- Suite 261117-030521-6408df (43/43) and TSan 261117-030527-4555c0 (19/19) at journal:904037a.
- Mutant close-behind on `close_behind`, 261117-030534-8c79e2: killed on values ("recovery keeps
  both acknowledged batches" fails, among 6 failures).
- Mutant close-behind on the crash sweep, 261117-030542-f27966: killed on the refusal check, then
  an abort (exit 134, `Journal::close` throws on the forgotten group). Not a value kill (Q2).
- Mutant run-any-scheduler on `wake_refused`, 261117-030549-c16cac: killed on the error value.

## What I checked myself

Scripts under `Detent/.scratch/review-journal-host-guards-durability/`; rerun at a fix commit as
`campaign job submit --quest journal-host-guards --slot review-jhg-probe -- sh -c "sh <dir>/probe.sh <dir>"`.

- 261117-031714-5039e8 (`probe.sh`, `probe_test.cc`), exit 1:
  - `refusal_inert`: refusals below the durable sequence, at a pending record and at the
    in-flight one; after the turn, a refusal changes no `Disk::operations`, the table entry and
    the log's sequence; a close at exactly the bound is then taken with base 3. 9/9 ok.
  - `sealer_close`: F1, below.
- 261117-031717-117eb4 (`mutant_base.sh`), exit 0: `sequence < turns.sequence(group)` (the
  table-base term dropped) is killed by `close_behind` ("a reopened group's close below the
  table's base is refused"). Both terms of the bound are load-bearing and tested.

Settled by reading:

- Q1 bound: `TurnLog::sequence` is the last durable record (set in `completed()` and from recovery,
  turn_log.h:203, 286), so it is the last acknowledged one, and it survives a restart. The table
  base covers the window after `reopen_group` before the reopen record is durable, when the log
  ignores records through base. A close above the bound drops nothing durable (R1 is sound).
- Refusal writes nothing: `held`, `driver_of`, `staged`, `sequence` have no side effects and the
  throw precedes `update` (host.h:324-329); the probe confirms it on disk. A crash between a
  refusal and the next turn is therefore the same as one before it, and a close after a refused
  close behaves as a first close (the sweep's `cycle()` does exactly that).
- Store appends racing a close: anything pending or in flight is refused by `staged()` first
  (probe: pending and in-flight cases).
- Q3: `scheduler_ref::post_call` returns false only when no `post_call` is declared
  (task.hh:222-225); a declared one returns void, so it cannot refuse at runtime. inline and
  blocking schedulers never drop (unbounded deques; blocking's under its mutex, task.hh:489).
  `asio_scheduler` drops silently once closed (task_asio.hh:91, 94), but `close()` runs only after
  the loops joined, when no `run()` can serve a wake. The doc's thread-safety statement is
  accurate: asio's `post_call` is listed as any-thread (task_asio.hh:55), blocking's is
  thread-safe, inline's is a plain deque push "on its own thread" (task.hh:399).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A close at exactly the bound acknowledges a record that was never written. A writer's transaction sits in the group's sealed open record (sequence 2) with its `durable(7, 2)` waiter linked; `close_group(7, 1, ...)` is taken (nothing pending, 1 is the durable sequence), `forget` drops the sealer and its open record but leaves the waiter linked. After `reopen_group`, the reopen checkpoint reuses sequence 2, `completed()` resumes the waiter: `acked=1 error=''`. After a restart the log holds only checkpoint "m1" at 2; "t1" is nowhere. Without a reopen the waiter hangs forever. Pre-existing (journal:555b69f's close had no guard at all), but it contradicts the quest's outcome ("Host can't lose an acknowledged record through close") and is the "close racing an in-flight append" case for turn-seal consumers (strata). | 261117-031714-5039e8 `sealer_close`; turn_driver.h:130-140 (`forget` leaves `waiters_`), :240, :377 (`satisfied` compares sequences only) | Options: (a) `close_group` (or `TurnDriver::forget`) refuses while a `durable()` waiter of the group waits above its durable sequence, as it refuses staged records; (b) `forget` fails those waiters with an error so a later incarnation's sequence never satisfies them; (c) also refuse while the group is sealed, making the owner unseal first. (a) or (b) fixes the false ack; I'd take (a), the same refuse-and-write-nothing shape as A1. Test: `sealer_close` in `host_test` passing (no ack without the record), plus a mutant. The lead may split it into its own quest if strata closing a sealed group is not yet reachable. |
| F2 | no | gap | The crash sweep's refused close adds no crash coverage: it is always at `sequence - 1` with nothing staged, writes nothing, and under the mutant the sweep dies on the refusal check and an abort, never on a lost batch. The value proof of A1 is `close_behind` alone. | 261117-030542-f27966 log | Fine as is; optionally drop the claim "the crash sweep" from A1's evidence or let the mutant's stale close continue to recovery so the sweep's "keeps every acknowledged batch" check is what fails. |
| F3 | no | gap | The bound proves the close's sequence, not its manifest: `close_group(g, last, stale_manifest)` is taken, and the record at `last` need not be a checkpoint. `Journal::close` checks both (journal.h:357-360); a direct Host caller does not. | host.h:327 | Optionally check that the log's live record at `sequence` is a checkpoint when `sequence` equals the durable sequence; or document it as the caller's contract. |
| F4 | no | nit | A `wake()` posted to a closed `asio_scheduler` (or queued on a stopped io_context destroyed unrun) never runs `woken`, so the `Wakeup` ref it took is never released: 16 bytes per wake after teardown. Noted in the journal; not new. | task_asio.hh:90-97; turn_driver.h:189 | None needed; a teardown order note in turn-seal.md would do. |

## Not run

- No sanitizer or CI run of my own; TSan relied on 261117-030527-4555c0.
- F1 with `run()` on a real loop or with writer threads: the probe is single-threaded with
  `commit()`; the mechanism (waiter list untouched by `forget`) does not depend on the driver.
- Re-partition (`Host` with another L) between a refusal and the next turn.
- Detent's, consensus's and strata's schedulers were not exercised; I relied on the journal's
  survey that none calls `TurnDriver::run` or `wake()` today.

## Round 2: fix re-check

Commit: journal:cc95ba636dee73b494daef7bbd8d341505aea9a1
Verdict: APPROVE
Date: 2026-11-17

Read `git diff 904037a cc95ba6` (9 files) and the journal's R6/R7. Every question below is
answered by a job at cc95ba6.

## Evidence I relied on

- Suite 261117-042733-4089ce (43/43) and TSan 261117-042749-c43f37 (19/19) at journal:cc95ba6.
- Mutants at cc95ba6: keep-waiters on `sealer_close` 261117-042807-0ddf36 and on
  `sealer_close_crash` 261117-042815-331f75; reuse-sequence on `sealer_close`
  261117-042823-cca3f2; close-behind on the shared-store sweep 261117-042830-3f7a05. All killed.

## What I checked myself

Scripts under `Detent/.scratch/review-journal-host-guards-durability/`. Rerun them with
`campaign job submit --quest journal-host-guards --slot review-jhg-probe -- sh -c "sh <dir>/probe2.sh <dir>"`
and, per mutant, `--slot review-jhg-vk-<m> -- sh -c "sh <dir>/value_kills.sh <m>"`.

- 261117-045650-fb382b (`probe2.sh`, `probe2_test.cc`), exit 0, all checks pass:
  - `f1` (Q1): this is round 1's `sealer_close`, run through the real Store/Journal path. Record
    1 is the create's checkpoint, record 2 "t1" is the sealed open record with its waiter, and
    `Store::close` closes at 1. The waiter ends `group_closed`, and `retired` is 2. A
    `stage(7, 2)` is a `logic_error`. `Store::recover` reopens at record 3. A wait for 2, linked
    before or after the reopen, ends `group_closed`. Only "a1" (record 4) is acknowledged. After
    a restart the log holds `3c,4b`, replay gives exactly "a1", and "t1" is nowhere.
  - `gap` (Q2a): a second waiter at sequence 5 widens the retired range to 2..5, and the reopen
    record is 6. Without a restart, `TurnLog::read` of the live directory gives `6c,7b,8b`,
    neither lost nor damaged, with no placeholder at 2..5. After a restart (`retired` back to 0),
    the log alone gives `6c,7b,8b`. Replay hands out "a1" at 1 and "a2" at 2, and the next batch
    is 9.
  - `reuse` (Q2a): the reopen record is staged but power goes off before its turn. Before the
    restart, the log holds nothing of the group past base 1. After the restart, the reopen
    reuses sequence 2. A new wait for 2 is acknowledged by the new record, and recovery
    replays nothing.
  - `covers` (Q2b): reopen at 6, with base 1 and `retired` 5. The checks run both without a
    restart and after one:
    - The reopen checkpoint (covers 1) is accepted.
    - A checkpoint at 7 covering 6 is accepted.
    - With batch 7 acknowledged, one covering 6 is refused ("a checkpoint behind a record it
      would drop") and one covering 7 is accepted.
    - A checkpoint covering the retired 5 is refused, and so is `close_group(7, 5)`.
- Value kills (Q3), `value_kills.sh`. Each job applies the mutant, deletes the sweep's non-value
  check (the refusal check, or the closed-wait check), and requires a recovered-values failure:
  - close-behind, shared-store sweep: 261117-045456-df5baa, killed on "a reopened group has
    every acknowledged batch".
  - keep-waiters, `sealer_close_crash`: 261117-045500-47e89a, killed on "recovery keeps every
    acknowledged batch".
  - reuse-sequence, `sealer_close_crash`: 261117-045505-b3adca, survived (F5).

Answers:

- Q1: fixed at the cause. `forget` fails the linked waiter, and the reopen never reaches its
  sequence; nothing a restart loses is acknowledged (261117-045650-fb382b `f1`).
- Q2a: `read` and replay skip the gap with no placeholder, both without a restart and after one.
  After a restart, a retired sequence is reused only when the reopen record never became
  durable, and nothing observes its old meaning: the log never held it, and every waiter on it
  was failed (261117-045650-fb382b `gap`, `reuse`).
- Q2b: yes. The covers guard compares against the post-reopen last durable record, not the
  retired one. A checkpoint at the reopen point is accepted, and one below a post-reopen
  acknowledged record is refused, both without a restart and after one
  (261117-045650-fb382b `covers`).
- Q3: yes, on values for close-behind and keep-waiters with the non-value checks removed
  (261117-045456-df5baa, 261117-045500-47e89a). reuse-sequence is caught only by the unit test.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Resolved. A close with a sealed open record fails its waiters with `group_closed`, and the reopen follows the retired sequences, so no reopened record acknowledges the dropped one, whether its waiter linked before the close, between the close and the reopen, or after the reopen. | 261117-045650-fb382b `f1`; 261117-042807-0ddf36, 261117-042815-331f75, 261117-042823-cca3f2 | None. |
| F2 | no | gap | Resolved. The sweep's stale close now uses a reopened group's real record, and a stale close taken under the mutant fails the sweep on the reopened batches. | 261117-045456-df5baa | None. |
| F3 | no | gap | Unchanged from round 1: the bound checks the close's sequence, not its manifest. | host.h:327 | As in round 1, optional. |
| F4 | no | nit | Unchanged from round 1: a wake after asio teardown leaks its `Wakeup` ref. | task_asio.hh:90-97 | As in round 1. |
| F5 | no | gap | The reuse-sequence mutant survives `sealer_close_crash`: the sweep has no wait that links after the close. That case, the false acknowledgement a reuse would give, is proven only by `sealer_close`'s "a later wait for sequence 2" check (and this review's `f1`). | 261117-045505-b3adca; 261117-042823-cca3f2 | Optional: in one sweep round, add a late `durable()` for the closed record after the reopen. |
| F6 | no | nit | After a restart, a retired sequence whose reopen record never became durable is reused (`reuse`: the reopen is 2). This is the same as any never-durable sequence after a crash, and nothing durable holds the old meaning. For consumers (strata `await_durable`, group-core's bundle position), (group, sequence) is an identity only once acknowledged; within one driver's life it never repeats. | 261117-045650-fb382b `reuse` | Optional: one sentence in turn-seal.md "Closing drops the open record". |
| F7 | no | nit | After a close, a `durable()` at or below `retired` fails with `group_closed` even for a record that was durable before the close (R6 accepts this: safe, merely unacknowledged). A group-core waiter that links late on an already durable bundle position sees `group_closed`, not success. | turn_driver.h:364 (`await_ready`) | None needed; documented in turn-seal.md. Owner's call whether a durable sequence should answer success instead. |

## Not run

- Writer threads with `run()` on a real loop around the close. The TSan job covers the existing
  thread tests, and my probes are single-threaded.
- A Host destroyed and recreated within one process: `retired_` lives with the driver and goes
  with it, as on a restart.
- Re-partition (a Host with another L) after a close with retired sequences.
