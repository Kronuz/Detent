# stage-5b-relays: Stage 5B: durable relay host and network fan-out

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-5b-relays`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 5 and `docs/RELAY_ARCHITECTURE.md`.

## Outcome

`detent-relay`: a durable fan-out daemon with private bounded configuration, TLS, readiness, status and health, which agents subscribe to like the authority.

## Acceptance criteria

- A1 [artifact] Relay host and network fan-out, service packaging: `docs/RELAY_ARCHITECTURE.md`, `docs/RELAY.md`
- A2 [artifact] Gate: consolidated Architect review with no remaining P1 or P2: `docs/RELAY_ARCHITECTURE.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
