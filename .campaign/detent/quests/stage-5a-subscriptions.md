# stage-5a-subscriptions: Stage 5A: resumable subscriptions and snapshot/history handoff

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-5a-subscriptions`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 5 and `docs/SUBSCRIPTIONS_ARCHITECTURE.md`.

## Outcome

Durable resumable subscriptions per namespace with exact snapshot and history handoff and durable publication feedback.

## Acceptance criteria

- A1 [artifact] Resumable streams and namespace subscriptions with snapshot and history handoff: `docs/SUBSCRIPTIONS_ARCHITECTURE.md`
- A2 [artifact] Gate: exact snapshot handoff, slow clients, retention resets, bounded memory and disk; consolidated acceptance with no remaining P1 or P2: `docs/SUBSCRIPTIONS_ARCHITECTURE.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
