# journal-tail-second-sector: journal

State: ready for review
Quest: .campaign/detent/quests/journal-tail-second-sector.md (the lead's; never edit it)

## Doing

Nothing: only the durability review remains. Head journal:1ce0043 (docs-only over journal:421011a,
where the jobs ran).

## Next

1. Fold in the durability review's findings by id.
2. Merge main once xapiand:journal-turn-buffers lands (it changes turn_log.h's turn-buffer path;
   mine touch only damaged_fragment and scan_segment).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261121-103735-cf5964 (T9 200 seeds, macOS, at journal:421011a: 1,228,757 checks, 0 failures; 18,365 damaged crossing fragments reported) |
| A2 | 261121-103735-cf5964 (macOS), 261121-103744-ab7d1e (FreeBSD VM), both 1,228,757 checks, 0 failures; fails without: 261121-103815-c377f1 at journal:ba19ae0 (the three stale_record_tears cases fail). At journal:72e6ab3 both rows failed: 261121-084910-a2210b, 261121-084913-af37f0 |
| A3 | 261121-111814-e34656 (seed 3 @4096 passes at journal:421011a); fails without: 261121-103756-644f6c at journal:b9e8c5f (the crossing pattern fails) |
| A4 | journal docs/mechanisms/turn-log-damage.md: crash model (what proves a sector), rule 2, Limits, Evidence (journal:8912780, journal:72e6ab3, journal:421011a, journal:1ce0043) |
| A5 | ci:macos-arm64 (CI run 44936652808 green at journal:421011a; the run at the docs-only journal:1ce0043 is queued) |

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

- The 200-seed false positives (seeds 29, 136 @2048; 189 @4096; reproduced locally at journal:72e6ab3,
  `journal_turn_log_power_test 1 29 2048`): the proof that fired was the fragment's own header, by
  the "header no writer emits" clause, not `written` nor the next-sector rule. The stale block
  (segment 1, block 0) held a record whose fragment starts 25 bytes before the torn fragment:
  its turn sat where the checksum, length, type and reserved byte go (length 0, type 0), its
  group 2 and sequence 1 where segment 2 and epoch 1 go. Pre-existing on main (main's
  damaged_fragment has the same clause); exposed by this quest's simulated_tears.
- Fix: segment and epoch alone no longer prove a header; it also needs a writer's type (1-4) and a
  zero reserved byte before them. A stale record's or manifest's integer before such a pair is a
  turn or sequence under 2^48, whose type and reserved bytes are zero. A bad type or reserved byte
  is damage only when the checksum verifies with it corrected (all four types tried, reserved 0),
  as a damaged length already was. A zero or past-block length with a writer's type and reserved
  byte stays damage, as on main. The residual (a stale payload holding a type byte 1-4, a zero, the
  segment and the epoch) is stated under Limits.

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
