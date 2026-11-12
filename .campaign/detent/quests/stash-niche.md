# stash-niche: does stash win on its own shape?

Risk: low. The lead writes this quest; contributors read it and never edit it.

## Outcome

Before Kronuz/stash is archived (D70 retires it as a timer wheel), one measurement on the workload
its design fits: many producers, one consumer draining in key order, no cancel and no re-arm,
keys spread over a wide horizon (sparse, levels allocated on first use). If stash beats (f) there,
its README narrows to that use and it stays; if not, retiring it stands.

## Acceptance criteria

- A1 Rows as `exclusive` jobs on the wheel-evidence harness (stash pinned at stash:d249352), stash
  (b) against (f): P = 1, 4, 14 producers, fire-and-forget only, horizons of 1 s and 24 h at 1 ms
  resolution, 1,000 and 1,000,000 live keys; time per insert, per drained value, and peak memory;
  A/A pairs per D51.
- A2 [artifact] `docs/design/stash-niche.md`: the rows, the A/A bands, and the verdict, written
  against a rule fixed before the first measured run: stash keeps a use only where it is at least
  1.10x better than (f) on every row of that use.

## Decisions

- Owner, 2026-11-11: stash "sounded like a really good idea"; check for other good uses before
  accepting retirement.

## May land early

Nothing.
