# retirement-close-race: Fix the RetirementDomain::close lost-wakeup deadlock

Risk: high (concurrency in the view-retirement path every reader uses). The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status retirement-close-race`.

## Outcome

`RetirementDomain::close` can no longer strand the retirement worker. Today close signals the
worker before it sets the closed bit; the worker reads the resident count, takes the wakeup, then
rereads to decide whether to keep checking, and if the bit lands between the two reads it sleeps
forever on a domain that could drain. Found by namespace-s1-s2 (journal, Detent:e36937c): 5 of 11
Debug runs of T7's d1-revert mutant hung, and lldb showed this state on three. After this quest
close is ordered so the worker always observes either the bit or a later wakeup, and a test that
races close against the worker's two reads fails without the fix.

## Acceptance criteria

- A1 The race reproduced on main's code: namespace-s1-s2's probe (261109-085248-0c7d20, exit 1 = reproduced) or an equivalent test, as a job at main that fails: the job
- A2 A regression test in `tests/detent_distribution/test_retirement.cc` that drives close between the worker's two reads (a seam or a deterministic interleaving, not sleeps) passes with the fix and fails with it reverted: a positive job and a revert job
- A3 The probe's 300,000 rounds drain with the fix: the job
- A4 TSan clean on the retirement tests: the `tsan` gate
- A5 The whole suite passes: the `native` gate

## Contracts

None changed: `RetirementDomain` and `RetirementRuntime` keep their API.

## Decisions

1. Split from namespace-s1-s2 (lead, 2026-11-09): the defect is on main and outside S1/S2's
   files; S1/S2's T7 tears down through `RetirementRuntime::shutdown`, which does not race, so S1/S2
   lands independently.
2. A mechanism note (`docs/mechanisms/retirement*`) only if the ordering is not obvious from the code.

## May land early

None.
