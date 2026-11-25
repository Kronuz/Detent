# client-local-source: Client local source design: Reader in every language

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status client-local-source`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/client-local-source.md`.

## Outcome

`docs/design/client-local-source.md` replaces the Client and its sources with `Reader` over the C ABI in C++, Python and Rust (B1-B6), approved by the owner with the Client and `health()` deleted.

## Acceptance criteria

- A1 [artifact] The draft: D1-D6, T1-T6, Q1-Q2, B1-B6: `docs/status/client-local-source.md`
- A2 [artifact] The owner's rulings recorded: Q1 (a) deletes `detent::Client`; Q2 (a) drops `health()`; D6 item 4's deletion list filled: `docs/status/client-local-source.md`
- A3 [artifact] B1-B6 wait only on reader S6 reaching main: `docs/status/client-local-source.md`

## Contracts

Defines the bindings built by `legacy-h`.

## Decisions

1. Owner approval 2026-11-04, verbatim: "Delete Detent::Client and health() if they're legacy." (D14)
2. Finding: no CI workflow built either binding; T6 adds the job.

## May land early

None.
