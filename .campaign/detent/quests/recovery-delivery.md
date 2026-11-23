# recovery-delivery: Recovery and delivery fixes, qualified component

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status recovery-delivery`. It landed before this
campaign, in Codex's Stage 6 work, which kept no ledger for it; its evidence is `docs/reviews/recovery-delivery-component-2026-10-25.md`.

## Outcome

Image decoding drains pending reclamation before admitting another slice; subscription idle expiry exempts accepted requests and active transfers; the optional delivery allowance is finite and validated; active-export counters exclude failures. The component passes the composed matrix and a complete 5M SDK verification.

## Acceptance criteria

- A1 [artifact] Source findings 1-4 closed: reclamation drain in image decoding, idle expiry of active work, a finite delivery allowance, honest export counters: `docs/reviews/recovery-delivery-component-2026-10-25.md`
- A2 [artifact] Acceptance condition 1: the composed matrix for the production changes, sanitizers and both macOS architectures: `docs/reviews/recovery-delivery-component-2026-10-25.md`
- A3 [artifact] Acceptance condition 2: every record of the preserved 5M corpus verified through the independent SDK, the reduced topology reported as such: `docs/reviews/recovery-delivery-component-2026-10-25.md`
- A4 [artifact] Acceptance condition 3: the two-relay/two-agent trial stays unqualified: `docs/reviews/recovery-delivery-component-2026-10-25.md`

## Contracts

Delivery session idle and allowance semantics; export counters.

## Decisions

1. No docs/status ledger: the evidence is the component review and the `docs/status/README.md` row "Recovery/delivery" (2026-10-29).
2. Sustained capacity and fast client attachment stay open (RC-3, RC-2).

## May land early

None.
