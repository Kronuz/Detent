# consensus-driver-pipelined-sim: The driver simulation runs pipelined appends end to end

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-driver-pipelined-sim`.

## Outcome

consensus-driver-flow's review F1 (`.campaign/detent/reviews/consensus-driver-flow-correctness.md`):
the driver simulation with every group at Flow{4, 16 KiB, 2 KiB} and Flow{4, 4 MiB, 256 KiB} (jobs
261123-114136-6d74f6, 261123-114139-580b1d) printed the same counters as the default, so it likely
never had a second append in flight; a follower Worker receiving pipelined appends through a
Driver is untested end to end. Xapiand's strata sets Flow{4, ...}, so this is proved before it does.

## Acceptance criteria

- A1 The driver simulation runs a pipelined configuration in which a counter it prints shows more
  than one append in flight on most seeds, with every oracle holding over at least 60 seeds: jobs
- A2 A `--fails-without` mutant in the follower's handling of a pipelined append (an out-of-order
  or overlapping append accepted) fails A1
- A3 The suite passes and `ci:ci` is green at the landed commit

## Contracts

None changed.

## Decisions

Lead, 2026-11-23: a follow-up of consensus-driver-flow's review (non-blocking F1).

## May land early

None.

## Tasks

None.
