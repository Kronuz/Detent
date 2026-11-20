# journal-hop-on-task: journal

State: ready for review
Quest: .campaign/detent/quests/journal-hop-on-task.md (the lead's; never edit it)

## Doing

Nothing. CI run 44659451731 at journal:485cd23 is still queued on GitHub.

## Next

1. Correctness review; confirm CI 44659451731 green.

## Blocked on

Nothing. A1 is recorded as D129: journal:904037a does not close F6; the fix is the await_suspend
refusal. Its evidence: `cancel_bare_home` (journal:8a79661, test only, on main) failed in
261119-181300-c03364 on "the cancelling thread leaves the waiter linked for the loop".
`Cancel` posts to the `durable()` caller's home, which 904037a's `run()` check never covers.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | D129 (its probe is named there; a failing probe cannot be cited as passing evidence) |
| A2 | 261119-181456-c79217; fails without: 261119-181502-b6d697 (bare-home mutant) |
| A3 | 261119-181505-2fa91c (TSan), 261119-181508-6b48ee (suite) |

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
