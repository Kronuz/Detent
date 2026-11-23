# reader-s1-s4: Reader S1, S2 and S4: counters, evaluate, GenerationWriter

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-s1-s4`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/reader-s1-s4.md`.

## Outcome

Read counters behind `DETENT_READ_COUNTERS` cost nothing when off; `evaluate` is one function outside `ReadView::try_eval`; `GenerationWriter` publishes real generations from C++ and C for tests.

## Acceptance criteria

- A1 [artifact] S1 counters behind `DETENT_READ_COUNTERS`, values pinned by hand, object code identical with the flag off: `docs/status/reader-s1-s4.md`
- A2 [artifact] S2 `evaluate` lifted out of `ReadView::try_eval`, matching on 4,680 combinations: `docs/status/reader-s1-s4.md`
- A3 [artifact] S4 `GenerationWriter` in C++ and C, read back through `PreparedReader` across six revisions: `docs/status/reader-s1-s4.md`
- A4 [artifact] A dying mutant per claim (six killed): `docs/status/reader-s1-s4.md`
- A5 [artifact] ASan+UBSan and TSan in separate builds pass the touched tests: `docs/status/reader-s1-s4.md`
- A6 [artifact] A mechanism note per change: `docs/status/reader-s1-s4.md`

## Contracts

Provides `DETENT_READ_COUNTERS`, `detail::evaluate` and `GenerationWriter` (C and C++).

## Decisions

1. Design vs code: `evaluate` takes `ns` and `key`; the C writer takes defaults-file JSON.

## May land early

None.
