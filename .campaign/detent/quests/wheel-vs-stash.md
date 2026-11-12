# wheel-vs-stash: The landed wheel against stash, same harness

Risk: low (measurement). Repo: home (Detent). The lead writes this quest; contributors read it and
never edit it. Machine state: `campaign status wheel-vs-stash`.

## Outcome

`docs/perf/wheel-vs-stash.md` gives the landed Kronuz/wheel (`wheel.hh` at wheel:bfd72bc,
fetched, not a copy) and stash (scheduler's pin) on the same workloads in the same build of
`docs/design/async-drivers-bench`, as plain numbers: ns per operation (arm, re-arm, cancel,
expire) at 1,000 and 1,000,000 entries, total bytes per entry, ns per cross-thread arm at P = 1,
4, 14, and stash-niche's 24 h fire-and-forget rows. No prototype or candidate arm appears in the
results: the owner asked for the library's real numbers and, where it is apples to apples, stash's
beside them. The wheel README, its docs and the "Who Owns the Wheel" article are rewritten from
this file afterwards (wheel-followups A4, and the lead for the article).

## Acceptance criteria

- A1 Each row from an `exclusive` job at a named commit, the D77 paired statistic (median over
  rounds with a 95% interval, A/A band) with stash as the reference, macOS; correctness first (both
  match the harness's reference model).
- A2 [artifact] The tables, each row's job id, and one line per row saying whether the comparison
  is apples to apples (same work per operation) or only plain numbers side by side, with the reason:
  `docs/perf/wheel-vs-stash.md`.
- A3 [artifact] The harness's evidence-era copies (`wheel.hh`, `stash_niche_wheel.hh`) are removed
  or no longer built, so nothing in Detent measures a prototype: `docs/perf/wheel-vs-stash.md`:
  "Harness".

## Contracts

Consumes `wheel:timer_wheel`. Provides nothing.

## Decisions

D72, D74, D77, D83.

## May land early

Nothing.
