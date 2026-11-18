# m1-pins: M1: Kronuz pins at their heads, with the W5 evidence

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status m1-pins`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/m1-pins.md`.

## Outcome

All 33 Kronuz pins equal their library heads and the libraries Detent pins agree with each other, checked in CI; the libraries' own pins moved first with green CI; the old and new builds are compared by input class and on the hot path, and the CHANGELOG and dependency docs record the move.

## Acceptance criteria

- A1 [artifact] M1 1: every pin equals its library's head, outside every "Commits not to pin" range, and equals each declaring library's pin; a CI check fails on a mismatch: `docs/status/m1-pins.md`
- A2 [artifact] M1 2: completion, journal and consensus pin their dependencies at heads, each with CI green on every job: `docs/status/m1-pins.md`
- A3 [artifact] M1 4: the full native suite and hosted CI pass, ASan/UBSan and TSan included: `docs/status/m1-pins.md`
- A4 [artifact] M1 5: the input classes are diffed through the old and new builds; snapshot JSON, wire bytes and persisted images are identical, each difference traced to a library commit: `docs/status/m1-pins.md`
- A5 [artifact] M1 6: the hot path is measured at old and new pins with an A/A control under `quiet exclusive`: `docs/status/m1-pins.md`
- A6 [artifact] M1 7: the CHANGELOG names each library moved, old and new pin, and the results that changed: `docs/status/m1-pins.md`
- A7 [artifact] M1 8: `docs/DEPENDENCIES.md`, `docs/DEPENDENCY_UPGRADES.md` and the website pages are current: `docs/status/m1-pins.md`

## Contracts

`cmake/dependencies.json` at heads; `tools/check_pins.py` in CI (`Detent:1098592`).

## Decisions

1. The owner authorized the scheduler, logger and http-log pushes; each library's main was fetched first and its CI green before the next.
2. Open owner items from W5: the 2.6% managed first-read loss, and http-parser's strict mode closing rejected requests without a response.

## May land early

None.
