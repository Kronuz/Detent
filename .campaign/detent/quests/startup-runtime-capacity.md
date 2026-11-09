# startup-runtime-capacity: Stage 6: startup runtime capacity configuration

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status startup-runtime-capacity`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/CAPACITY_RUNTIME.md` and the status index row "Startup runtime capacity" (2026-10-29).

## Outcome

Runtime capacity limits are carried coherently through the authority, relays, managed images and the SDK without changing defaults or profile equality.

## Acceptance criteria

- A1 [artifact] Coherent runtime limits through authority, relay, managed images and SDK; defaults and profile equality unchanged: `docs/CAPACITY_RUNTIME.md`
- A2 [artifact] Native qualification of the frozen candidate: `docs/CAPACITY_RUNTIME.md`
- A3 [artifact] Larger cardinalities, CLI reader integration and online profile activation: `docs/CAPACITY_RUNTIME.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
