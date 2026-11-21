# journal-tail-second-sector: journal

State: waiting on jobs 261121-084910-a2210b 261121-084913-af37f0
Quest: .campaign/detent/quests/journal-tail-second-sector.md (the lead's; never edit it)

## Doing

Jobs at journal:72e6ab3 (fix + T9 patterns) and the mutant journal:3955aad (branch
mutant/journal-tail-second-sector/tear). CI runs on the pushed quest branch.

## Next

1. When the jobs pass: fill the doc's Evidence numbers (200-seed counts), Evidence table, CI run id; State "ready for review".
2. Merge main once xapiand:journal-turn-buffers lands (msg from lead: it changes turn_log.h's turn-buffer path; mine touch only damaged_fragment and scan_segment).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261121-084910-a2210b (T9 200 seeds, macOS, with the crossing pattern), pending |
| A2 | 261121-084910-a2210b (macOS), 261121-084913-af37f0 (FreeBSD VM), pending |
| A3 | 261121-084916-f70fcc passed: mutant journal:3955aad fails seed 3 @4096 with 8 failures, all in the crossing pattern ("the damage is reported", "a group not lost keeps") |
| A4 | journal docs/mechanisms/turn-log-damage.md: crash model (what proves a sector), rule 2's new case, Limits (journal:8912780, refined in journal:72e6ab3) |
| A5 | CI run 44926626010 at journal:72e6ab3, queued |

## Result

- Rule (docs/mechanisms/turn-log-damage.md): a sector is proven by a later verifying fragment, by
  the fragment's own header naming segment and epoch, or by a whole header of the fragment's
  write starting before it in the sector (block header, or a predecessor that does not end its
  write). A crossing fragment whose first sector is proven, with a verifying fragment of the
  segment in the next sector, is damage.
- Code: `damaged_fragment(..., bool written)`; scan_segment tracks where the last
  same-write header starts (`in_write`).
- T9 adds `crossing_damage` (P1 shape, every block) and `simulated_tears` (P6 plus old filled
  sectors with later ones written, and an old first sector). Local 8 seeds: 0 failures, 48,239
  checks; at 2048 and 4096: 696 crossing cases asserted, 31 with a cut header only counted.
  The mutant at 30 seeds @2048: 122 failures.

## Rulings I made

- A whole header is the only proof of a sector. A first version also accepted any verifying
  bytes of the same write before the fragment; T9 crash point seed 5 cut 116 @1000 read a real
  tear as damage, because the old sector held the same payload letters the write did. Payload
  bytes can equal old bytes (zeros, stale records), so they prove nothing.
- A fragment whose header the sector boundary cuts, with no header of its write before it in the
  sector, stays a tear (a crash can leave its first sector old with the next new): stated as a
  limit, counted in T9 ("more with a cut header"), not asserted.
- The next-sector scan uses `verifies_in_sector`, as rule 2's same-sector case does, so a
  fragment whose header the next boundary cuts counts, at the same ~2^-32 exposure.
- The mutant drops only the next-sector line, so it restores the tear reading exactly.
