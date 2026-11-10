# fixture-500k: Coordinator fixture builds the 500K snapshot only for replies that carry it

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status fixture-500k`.

## Outcome

The delivery coordinator test fixture builds `response(count)` only for the replies that send the
snapshot (phases 1 and 5 and the non-subscription modes), so the seven 500K subscription cases no
longer time out the coordinator's 1,000 ms release_session under load. Brings Detent:203f729 from
dense-pages to `main` so every quest's `native` gate is green; dense-pages merges it back unchanged.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 The full native suite passes, the seven `*-500000` coordinator cases included: the `native` gate
- A2 The six 500K subscription cases pass 6 rounds in a row at -j4 (`tests/detent_delivery/repeat_500k.sh 6`): a job
- A3 Revert check: with `tests/detent_delivery/mutants/snapshot-eager.patch` applied, `repeat_500k.sh` fails: a job

## Contracts

None; test fixture only.

## Decisions

Lead: land the fixture fix ahead of dense-pages (started with --force over dense-pages' `tests/**`); the change is identical on both branches.

## May land early

None; the quest is one commit.
