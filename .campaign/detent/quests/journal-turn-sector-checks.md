# journal-turn-sector-checks: Turn log format RFTW0002: per-sector checks so a torn newest turn is never lost silently

Risk: high (persistent format, durability). The lead writes this quest; contributors read it and
never edit it. Its machine state (status, branch, checks, reviews) is in `state.json`: `campaign
status journal-turn-sector-checks`.

## Outcome

The turn log implements the design accepted in journal-tail-cut-header-design
(`docs/design/turn-log-cut-header.md`, journal:9e0649d, durability review APPROVE), option O3b:
every sector of a turn write carries an 8-byte trailer (the segment's low 32 bits, CRC-32C over
the sector and all 64 bits of the segment), so recovery tells a torn sector from a damaged one,
a stale sector from a recycled file from a current one, and reports damage instead of reading it
as a tear. The format becomes `RFTW0002`; `RFTW0001` is no longer read (greenfield, no users). The
earlier-turn rule from journal-earlier-turn-loss (rule 6) stays (design D4, F3).

## Acceptance criteria

- A1 The crash-model probe (the design's, at its landed commit as an extra `--ref`) at the fix
  reports 0 of 201 newest-tail cases silent (36 at main) and 0 of 4807 earlier-turn cases silent:
  a job.
- A2 Each of D4's sector verdicts (current, torn, stale from a recycled segment, damaged, all-zero,
  unreadable) has a deterministic test in the turn log tests checking recovery's result by value;
  a job runs them at the fix, and `--fails-without` at journal main for the newest-tail shapes.
- A3 A stale sector from segment S - 2^32 at the same index in a recycled, unzeroed file is read
  as stale, never as current: a test, and a `--fails-without` mutant that binds only 32 bits.
- A4 Cost against main, strict jobs at named commits with the A/A band, interleaved: bytes written
  per turn within the design's numbers (-5.6% to -6.2% for records up to 700 B, about +1.0% at
  4 KiB), and append latency p50/p99 with no change beyond the band; a regression beyond the band
  goes back to the lead.
- A5 [artifact] `docs/mechanisms/turn-log-damage.md` describes RFTW0002's verdicts and states the
  Limit after it, including the review's F6 (a sector more than 2^32 segments old reads as damage)
  and F7 (an all-zero sector is not "verifies under neither"), and journal-earlier-turn-loss's
  review F1 (a CRC-valid fragment forged inside a torn write's payload reads as damage: every
  unmanifested group is reported lost, never silently).
- A6 The turn-log fixtures are regenerated as RFTW0002 and the fuzz target reads them; the suite
  passes and `ci:ci` is green at the landed commit.

## Contracts

The on-disk turn log format: `RFTW0001` → `RFTW0002` (owner decision D147). No reader of the old
format remains.

## Decisions

D147 (owner, 2026-11-23): go for O3b.

## May land early

None: a format change lands whole.

## Tasks

None.
