# drivers-completion-seam: async-drivers 2: completion seam (SyncData, completion_ref, Inline/HeldCompletion)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status drivers-completion-seam`.

## Outcome

Kronuz/completion gains the seam the drivers run through (`docs/design/async-drivers.md` (Detent), Build plan row 2): `SyncData` on every backend, `completion_ref` with a token, `InlineCompletion` and `HeldCompletion`, and `drive_synchronously` becomes `InlineCompletion`'s loop. It is additive; existing callers are unchanged.

## Acceptance criteria

- A1 `SyncData` works on every backend, including under injected faults: fault tests per backend
- A2 `completion_ref` with a token, `InlineCompletion` and `HeldCompletion` exist, and `HeldCompletion` lets a test hold, reorder and release completions step by step: tests
- A3 `drive_synchronously` is `InlineCompletion`'s loop, with no second implementation, and existing callers and suites are unchanged: the suite and a grep job
- A4 Turn primitives are ordered ahead of artifact and snapshot I/O, measured before and after at named commits (X13): an exclusive job pair

## Contracts

Provides completion's seam (`completion_ref`, `InlineCompletion`, `HeldCompletion`, `SyncData`) for journal-driver and consensus-driver.

## Decisions

async-drivers split (D96, lead): quest 2 of the design's build plan; Q5 (task and completion join the split) follows the design's recommendation.

## May land early

Everything: it is additive.
