# rc1-integration: RC-1: the remaining required integration and its closure

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rc1-integration`. Planned from `docs/RELEASE_PLAN.md` "Remaining release checklist" RC-1 and its component rows (Consensus/journal adoption, Daemon and HTTP logs, Retained-state resources), and `docs/CONSOLIDATION_RESPONSE.md` CL-2;
the criteria are the plan's own, not new ones.

## Outcome

Every RC-1 item not owned by the four adoption quests lands or is rejected with evidence, and the exit's closure holds for all of RC-1.

## Acceptance criteria

- A1 CL-2 owner-mode forwarding: one owner-lock protocol, Detent in `both` mode (`docs/CONSOLIDATION_RESPONSE.md` CL-2): jobs
- A2 Finalized consensus transport: Track C transport complete, coherent pins, old-state recovery, mixed-version refusal and rollback qualification (release plan, Consensus/journal adoption): jobs
- A3 Asynchronous authority and relay HTTP log completion observer, with composed-product checks (release plan, Daemon and HTTP logs): jobs
- A4 Retained-state resources: default footprint and final transport dependency closure (release plan row): jobs
- A5 Closure: each RC-1 component (EX-1 to EX-4, CL-2, consensus transport, HTTP logs) has reviewed public pins, its private engine deleted, compatible bytes and failure and lifetime checks, recorded separately: a composed full suite and sanitizers on `main`

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. EX-1 to EX-4 are `resource-adoption`, `http-client-adoption`, `msgpack-adoption`, `persistent-map-adoption`.
2. Track C is strata's (the Xapiand campaign's); A2 is ordered `--after xapiand:<id>` once that quest exists.
3. The lead splits A1 to A4 into their own quests when M2 starts; A5 is the milestone's gate.

## May land early

None until the lead splits it.
