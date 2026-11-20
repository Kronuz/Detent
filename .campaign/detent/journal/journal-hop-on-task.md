# journal-hop-on-task: journal

State: waiting on jobs 261119-181300-c03364 261119-181456-c79217 261119-181502-b6d697 261119-181505-2fa91c 261119-181508-6b48ee
Quest: .campaign/detent/quests/journal-hop-on-task.md (the lead's; never edit it)

## Doing

Jobs queued at journal:8a79661 (A1 probe) and journal:485cd23 (fix, A2, A3). Branch pushed.

## Next

1. Read the jobs: 261119-181300-c03364 must exit nonzero (the F6 failure at main's code); the
   rest must pass (the mutant job passes when its test fails).
2. Lead: record A1's decision (PLAN.md Decisions id), then a correctness reviewer.

## Blocked on

lead: A1's decision id. Evidence and recommendation:

- journal:904037a does not close F6. It makes `run()` refuse a loop without `post_call`
  (turn_driver.h `run`), but `Cancel` posts to `waiter.home`, which is the `durable()` caller's
  `task::current()` (await_suspend), never checked. A waiter homed on a scheduler without
  `post_call` still ran `hop` on the cancelling thread, unlinking it from the driver's list there.
  `Cancel` is unchanged from 904037a to journal:9e11713 (main).
- Probe: `cancel_bare_home` (journal:8a79661, test only, on main) is `cancel_hops` on a
  scheduler declaring only post/now: cancel on another thread between drives, join, assert the
  waiter is still linked. Job 261119-181300-c03364; expected to exit 1 on "the cancelling thread
  leaves the waiter linked for the loop".
- Recommendation: record "904037a does not close F6; fixed in journal by refusing at
  await_suspend" (A2, done, below). No task change is needed: the primitive `hop` uses,
  `scheduler_ref::post_call`, exists at the pinned task:95798a0 and on task main; the review's
  "expose post_call in scheduler_ref" was already true. Task lacks only a side-effect-free
  "declares post_call" query; journal probes with a no-op post_call instead (see Rulings).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Decision id pending (lead). Probe 261119-181300-c03364 at journal:8a79661 |
| A2 | 261119-181456-c79217 (driver_test at journal:485cd23); 261119-181502-b6d697 (--fails-without, bare_home_refused at the bare-home mutant) |
| A3 | 261119-181508-6b48ee (suite); 261119-181505-2fa91c (TSan: journal_driver and turn_seal thread cases) |

## Result

journal:485cd23. `durable()`'s await_suspend refuses a home whose scheduler declares no
`post_call` (std::logic_error, before it links), so `Cancel` always posts `hop` to the caller's
loop and never unlinks off it. `detail::posts_calls` is the one probe, shared with `run()`'s
check. Test `bare_home_refused` (driver_test); mutant `bare-home` in test/turn_seal_mutants.sh;
mutants.py's `cancel-without-hop` text follows the new line. Docs: docs/mechanisms/turn-seal.md
("The loop's scheduler"), CHANGELOG.md 2026-11-19, durable()'s API comment.

## Rulings I made

- Refuse at await_suspend (the review's F6 fix) instead of asserting in Cancel, as task's
  cancellable sleep does: the refusal is synchronous on the caller, the assert would fire on the
  cancelling thread inside a noexcept callback.
- The probe is a queued no-op `post_call` per suspended wait (the mechanism run() already used),
  because scheduler_ref has no side-effect-free query. Cost: one queued no-op per suspension,
  beside a turn's write and fsync; not measured. A task-side `scheduler_ref` query would remove
  it from both places: optional follow-up for task, not needed for F6.
- A wait that does not suspend (await_ready) is not refused: it never registers a callback.
- `cancel_bare_home` (A1 probe) is replaced by `bare_home_refused` in journal:485cd23: the
  refusal removes its premise (a linked waiter on such a home).
