# dense-sync-stack-5m-times: Dense sync stack: cold bootstrap and restart times at 5M, exclusive

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dense-sync-stack-5m-times`.

## Outcome

dense-sync-stack lands on 100K exclusive times and 5M counts measured under light load. This quest adds the 5M cold bootstrap and restart times: the stacked arm against Dense alone, S2 alone and main, 2 rounds, each arm an `exclusive` job at its named commit, run in a quiet window agreed with the other campaigns sharing the machine.

## Acceptance criteria

- A1 [manual] Each 5M arm ran `strict` at its named commit; the lead checks the ids against `campaign job list`. (D135: strict, not exclusive)
- A2 [artifact] The 5M times beside the 100K table: `docs/perf/dense-sync-stack.md`

## Notes

- The fixture is `.scratch/capacity-trials/5000000-flat-r1` (D119: never deleted for disk).
- Ask the Xapiand lead for the window before submitting; the arms starved for hours on 2026-11-18 at priority -1.
