# prepared-activation: Managed prepared-reader default

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status prepared-activation`. It landed before this
campaign, in Codex's Stage 6 work, which kept no ledger for it; its evidence is `docs/reviews/prepared-activation-component-2026-10-26.md`.

## Outcome

Managed clients use prepared readers by default: coordinator and managed readiness wait for confirmed preparation, metadata probing uses the protected walker, and FreeBSD's ACL capability results are handled. A real 5M preparation, restart and 200 fresh first reads qualify the resident-data case.

## Acceptance criteria

- A1 [artifact] Coordinator readiness uses owner-held confirmed preparation (source closure 1): `docs/reviews/prepared-activation-component-2026-10-26.md`
- A2 [artifact] Managed readiness separates an absent empty store from an uncertain replacement (source closure 2): `docs/reviews/prepared-activation-component-2026-10-26.md`
- A3 [artifact] Metadata probing uses the protected walker; timeouts close and drain (source closure 3): `docs/reviews/prepared-activation-component-2026-10-26.md`
- A4 [artifact] FreeBSD ACL policy and EMLINK corrections, nine focused native FreeBSD checks (FreeBSD corrections): `docs/reviews/prepared-activation-component-2026-10-26.md`
- A5 [artifact] Exclusive macOS 5M qualification: preparation and restart, 400 SDK and 80 CLI checks, 200/200 live resident first reads below 1 ms (evidence and activation gates): `docs/reviews/prepared-activation-component-2026-10-26.md`

## Contracts

Managed readiness semantics; prepared readers become the managed default.

## Decisions

1. No docs/status ledger: the evidence is the component review, `docs/reviews/prepared-managed-qualification-2026-10-26.md` and the `docs/status/README.md` row "Fresh client attachment".
2. Cold storage, incremental refresh and source sharing stay open under RC-2.

## May land early

None.
