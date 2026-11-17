# journal-host-guards: journal

State: waiting on jobs 261117-042733-4089ce 261117-042749-c43f37 261117-042807-0ddf36 261117-042815-331f75 261117-042823-cca3f2 261117-042830-3f7a05
Quest: .campaign/detent/quests/journal-host-guards.md (the lead's; never edit it)

Code: journal worktree `journal-wt/detent/journal-host-guards`, branch
quest/detent/journal-host-guards; work commits journal:904037a, journal:cc95ba6 (fix round 1:
durability review F1, F2).

## Doing

Fix round 1 at journal:cc95ba6: jobs queued (below), not yet read.

## Next

1. Read the six jobs; check 261117-042830-3f7a05's log fails on "a reopened group has every
   acknowledged batch", not an abort (exit 1, not 134).
2. `campaign accept journal-host-guards durability --confirm F1=261117-042733-4089ce`.

## Blocked on

Nothing. CI (`check ci`) runs once the lead pushes the branch; I don't push.

## Evidence

Fix round 1, journal:cc95ba6 (queued):

| Job | What |
|---|---|
| 261117-042733-4089ce | suite, A1 A2 (incl. host `sealer_close`, `sealer_close_crash`) |
| 261117-042749-c43f37 | TSan `turn_seal|journal_driver|journal_host` |
| 261117-042807-0ddf36 | fails without: keep-waiters, host `sealer_close` (F1 probe as test) |
| 261117-042815-331f75 | fails without: keep-waiters, host `sealer_close_crash` (close with a sealed open record in flight) |
| 261117-042823-cca3f2 | fails without: reuse-sequence, host `sealer_close` (a late wait for 2 acked by the reopen) |
| 261117-042830-3f7a05 | fails without: close-behind, shared-store sweep, expected on values (F2) |

The review probe 261117-031714-5039e8 is not rerun as is: its reopen stages the checkpoint at
`base + 1` = 2, the retired sequence, which `stage()` now refuses by design (it would abort on the
uncaught throw). host_test `sealer_close` is the probe with the reopen as Journal stages it.

At journal:904037a:

| Criterion | Evidence |
|---|---|
| A1 | 261117-030521-6408df; fails without: 261117-030534-8c79e2, 261117-030542-f27966 |
| A2 | 261117-030521-6408df, 261117-030527-4555c0; fails without: 261117-030549-c16cac |

- Suite 261117-030521-6408df: 43/43 at journal:904037a, incl. `journal_host` (`close_behind`),
  `journal_shared_store` (crash sweep with a refused close before every cycle), `journal_turn_seal_wake_refused`.
- TSan 261117-030527-4555c0: 19/19 (`turn_seal_*` incl. `wake_threads`, `wake_refused`; `journal_driver`; `journal_host`), no report.
- Mutant close-behind, `close_behind` 261117-030534-8c79e2: the stale close is taken, and after
  retention and a restart recovery loses a1, a2 ("recovery keeps both acknowledged batches" fails).
- Mutant close-behind, crash sweep 261117-030542-f27966: "a close behind ... is refused" fails, then
  the Store's own close aborts on the forgotten group (exit 134).
- Mutant run-any-scheduler, `wake_refused` 261117-030549-c16cac: run() on a scheduler without
  post_call is not refused and sleeps deaf to wake().
- API doc: `include/journal/turn_driver.h` `wake()` and `run()` comments; `docs/mechanisms/turn-seal.md`
  "The loop's scheduler".

## Result

- A1: `Host::close_group` (include/journal/host.h) throws `logic_error("a close behind the group's
  last acknowledged record")` when `sequence < max(entry.base, driver.sequence(group))`, before
  any write. `test/host_test.cc` `close_behind`: group 7's real checkpoint record (seq s) with
  batches a1, a2 acknowledged after it; `close_group(7, s, its manifest)` is refused, the table
  stays open with no record, the log keeps sequence s+2; after 200 turns of another group and a
  restart, recovery returns bundle "state" and batches {a1, a2}. A reopened group's close below the
  table's base is refused; at the base it is taken. `test/shared_store_test.cc` `cycle()` tries
  `close_group(g, sequence - 1)` before every close, so the crash sweep crosses a refused close at
  every cut (it writes nothing, so the cut points are unchanged).
- A2: `TurnDriver::run` posts a no-op through `task::current().post_call` before it holds the
  driver and throws `logic_error` when refused (scheduler_ref refuses only a scheduler declaring no
  post_call, task:95798a0 task.hh:221-225). `loop_` is set only after that, so `wake()`'s post is
  never refused. `test/turn_seal_test.cc` `wake_refused`: run() on a scheduler without post_call
  throws, the driver stays free, the wake stays asked for (`!idle()`), and run() on an
  inline_scheduler seals it; a later wake while run() sleeps reaches it through post_call.
- Docs: docs/mechanisms/turn-seal.md (contract bullet, the blocking-entry wake sentence corrected,
  evidence row), docs/design/host-layout.md (Close), CHANGELOG.md 2026-11-17.

### Schedulers used with TurnDriver (for the lead)

- consensus (consensus:57745e8): never calls `TurnDriver::run` or `wake()`. Its blocking entry
  holds the turn driver as `Holder::entry` over `task::blocking_scheduler`
  (include/consensus/driver.h:980-986), whose post_call is thread-safe (task:95798a0
  task.hh:469-489). Same F6 shape in consensus's own Waiter: `Cancel` runs `hop` on the cancelling
  thread when post_call is refused (include/consensus/driver.h:455); outside my file set.
- Detent (Detent:f43fe38e): no TurnDriver use; `CompletionScheduler` runs on
  `task::asio_scheduler` (include/detent_delivery/completion_scheduler.hh:27), whose post_call is
  thread-safe (task:95798a0 task_asio.hh:89-97).
- completion (completion:7731315): declares no scheduler and calls no wake().
- strata (strata:491a1bdc): no `wake()` caller yet; the turn-seal consumer must run `run()` on
  asio_scheduler or blocking_scheduler to call wake() from writer threads.
- task's asio_scheduler drops a post_call once closed (task_asio.hh:91); closing happens after the
  loops join, when no run() serves, so no wake is lost, but that queued wake's `Wakeup` (16 bytes)
  is never released.

## Rulings I made

- R5 (F1, lead's shape): `TurnDriver::forget` fails every linked `durable()` waiter of the group
  with a new `group_closed` (runtime_error, carries group and sequence), via the waiter state
  `closed`; `Host::close_group` reaches it through forget. Waiters at or below the durable
  sequence are never linked, so every linked one is not durable.
- R6 (the incarnation question): the waiter's identity needs the incarnation only because a
  writer can take the open record's sequence before a close and link after it, or after the
  reopen (writer threads hop to the loop). Instead of an incarnation argument on `durable()`
  (a contract change consensus's `watch` would have to follow), forget retires the sequences an
  earlier incarnation could hand out: durable + 1 (the open record) and every linked waiter's,
  kept per group in `retired_` for the driver's life. `durable()` at or below `retired(group)`
  fails with group_closed (await_ready), `stage()` refuses a record there, `last_staged` (so
  `seal()`) continues after it, and `Journal::recover_shared` reopens at `max(base, retired) + 1`.
  Sequences then never repeat within a driver, so (group, sequence) stays a unique identity. A
  late wait for an already durable sequence is refused too: safe, merely unacknowledged.
  Cost: one map entry per group ever closed on the driver; a reopen skips one sequence.
- R7 (F2): the shared-store sweep's refused close now uses a reopened group's real record
  (sequence and manifest) once batches are acknowledged after it and no publication followed, after
  every round; a close taken there (the mutant) reopens the group from that record and the sweep
  fails "a reopened group has every acknowledged batch" on values.
- F3, F4 (non-blocking) unchanged.

- R1: A1's "last acknowledged record" is the larger of the log's durable sequence and the table's
  earlier `base` (a reopened group whose reopen checkpoint is not yet durable has only the table's).
  A close above it is still taken: it drops no acknowledged record, and `Journal::close` passes
  exactly its last record.
- R2: A2's refusal is surfaced at `run()`'s start (the caller choosing the scheduler sees it)
  rather than in `wake()`, which stays `noexcept` under the caller's locks. The probe costs one
  queued no-op per run() start. `wake()`'s post_call result is not checked: it cannot be false
  once run() admitted the loop.
- R3: The blocking entry is unchanged: `wake()` posts nothing there and the entry sees the flag at
  its next pass. From another thread under a deadline, the wake waits for the deadline or another
  post. Not a drop; a latency the lead may want as its own quest (register the entry's
  blocking_scheduler as the wake target).
- R4: The two mutants live in `test/turn_seal_mutants.sh` with the turn-seal ones (one script,
  one anchor check).
