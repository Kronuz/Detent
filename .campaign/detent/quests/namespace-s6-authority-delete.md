# namespace-s6-authority-delete: Authority v1 deletion: one-shot, v1 sessions, AuthorizedScope, old cursor (P5)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6-authority-delete`.

## Outcome

Nothing consumes v1 after namespace-s6-v1-removal, so the authority deletes it: the one-shot route `POST /v2/distribution`, the v1 session body, `AuthorizedScope`, `export_scope`, `subscription`, `DeliveryMode`, the old `Cursor` and `authority::NamespaceCursor` (the one W1 cursor replaces it), with docs and website pages describing them (P5 of D94, D43).

## Acceptance criteria

- A1 Q4: the one-shot route, the v1 session body and poll configuration are removed with nothing left behind: a grep job and the suites
- A2 W1: `AuthorizedScope`, `export_scope`, `subscription`, `DeliveryMode`, the old `Cursor` and `authority::NamespaceCursor` are gone and the authority uses `include/detent/cursor.hh`'s one type: a grep job and the suites
- A3 Docs and the website describe only v2 sessions: the docs check and a grep job

## Contracts

Removes the v1 wire from the authority; `/v1/health` and producer/recovery format versions stay (their numbers are independent).

## Decisions

D94 (lead, from the design review `reviews/namespace-s6-v1-removal-design.md`): P5.

## May land early

Nothing: it removes a wire.
