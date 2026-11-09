# fleet-runner: Stage 6 component 1: bounded runner and native fleet tooling

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status fleet-runner`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/STAGE6_ARCHITECTURE.md` component 1 and `docs/FLEET_QUALIFICATION.md`.

## Outcome

`tests/detent_system/` orchestration and `detent_fleet_load`, bounded in outstanding work, buffers, oracle state and artifacts, recording provenance for every run.

## Acceptance criteria

- A1 [artifact] Bounded runner and frozen evidence (component 1): `docs/STAGE6_ARCHITECTURE.md`, `docs/FLEET_QUALIFICATION.md`
- A2 [artifact] Gate: Architect source approval; the reduced native matrix passes: `docs/FLEET_QUALIFICATION.md`, `benchmarks/fleet-tooling-qualification.json`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
