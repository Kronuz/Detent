# w2-leftovers: stale-leader write probe; one await shape

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status w2-leftovers`.

## Outcome

Two w2 review findings never got a quest (PLAN Not quests yet 6; `docs/reviews/w2-s1-s8-code-review.md:43-49`): CR3, `run_partition.py` never writes to the stale leader and requires a refusal; CR4, Detent's `await_io` (`include/detent_transport/await_io.hh`, 4 uses) sits beside `task::await_asio` (9 uses), two shapes for one thing. After this quest the partition test writes to the stale leader and requires a refusal, and one await shape remains; whether it is `task::await_asio` or `await_io` moved into the task library is the architect's call, recorded as a decision.

## Acceptance criteria

- A1 The partition test writes to the stale leader and fails if the write is accepted (--fails-without at a mutant that accepts it): jobs.
- A2 [decision] Which await shape stays: a PLAN.md Decisions id.
- A3 One await shape in Detent (`git grep await_io` empty, or `await_asio` gone) and the suite passes: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U11).
