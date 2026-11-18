# m1-pins: journal

State: landed
Quest: .campaign/detent/quests/m1-pins.md (the lead's; never edit it)

History: landed at `Detent:3e8bdbc` on 2026-11-04, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/m1-pins.md`: "All 33 manifest pins equal their library heads and `tools/check_pins.py` reports 0 mismatches." |
| A2 | `docs/status/m1-pins.md`; `CHANGELOG.md`: "http-parser, http, journal, consensus), each CI-green before the next" |
| A3 | open: partial: the pin waves' CI 41796189324 is green, but W5's run 41835299429 has no terminal result (W5, CI) and no Python or Rust binding check is recorded in `docs/status/m1-pins.md` |
| A4 | `docs/status/m1-pins.md`: "Results per class in the note; every difference has a library commit as its cause." |
| A5 | open: partial: measured, but the managed first read is 2.6% slower and misses the sub-millisecond target on both builds; left to the owner (W5, Blocked on 1) in `docs/status/m1-pins.md` |
| A6 | `docs/status/m1-pins.md`: "CHANGELOG, DEPENDENCIES.md, DEPENDENCY_UPGRADES.md (`Detent:502e2d9`)" |
| A7 | `docs/status/m1-pins.md`: "and the website's dependencies, installation and roadmap pages (`Detent:f8c747d`)" |

## Result

Pins landed at `Detent:f7267d5` and W5 at `Detent:3e8bdbc` on 2026-11-04; pin-check `Detent:1098592`.
`tools/check_pins.py`: 33 libraries, 43 shared declarations, 0 mismatches; 13 library commits, each with green CI.
Native 237 tests pass (1 skip); final CI 41796189324 green on every job, pin-check, ASan/UBSan and TSan included.
W5 (`docs/perf/m1-w5-evidence.md`): every input-class difference has a library commit as its cause.
Hot path inside the A/A band except the managed first read: median 1,087.0 to 1,116.7 us (+2.6%, A/A 0.996).
W5 CI 41835299429 was dispatched; the ledger records no terminal result. No code review (evidence work).
Docs: `CHANGELOG.md`, `docs/DEPENDENCIES.md`, `docs/DEPENDENCY_UPGRADES.md`, website dependencies, installation, roadmap.
Left open: A3, A5.

## Rulings I made

Detent's three test `FakeIO` classes implement completion's new read-only opens; no `src` change.
