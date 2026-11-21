# journal-tail-second-sector: journal

State: ready to land (durability review accepted, F1=261121-115925-e0216a)
Quest: .campaign/detent/quests/journal-tail-second-sector.md (the lead's; never edit it)

## Doing

Nothing: F1 fixed and accepted at journal:e912a28; F2 and F3 are journal-tail-cut-header-design's.

## Next

1. Merge main once xapiand:journal-turn-buffers lands.

## Blocked on

Nothing.

## Review findings

- F1 (blocking): fixed at journal:e912a28. `verifies_in_sector` scans only offsets whose whole
  fragment header lies inside the sector (`p + fragment_header <= end`), so damaged_header, rule 1
  and the next-sector rule all need a header the sector holds whole. T9 `cut_header_tears`: for
  k = 1..23 bytes of G's cut header left as written in the old sector, the next-sector, same-sector
  and block-header shapes read as tears (69 checks). `crossing_damage` counts a case whose next
  fragment's header the boundary cuts as unproven. Mutant journal:409032c
  (branch mutant/journal-tail-second-sector/cut-header, local) restores the cut-header scan;
  fails-without job 261121-115934-0256e0: 1,146 checks, 69 failures (every cut_header_tears case). Local 8 seeds at e912a28: 48,308 checks, 0 failures;
  crossing 680 asserted + 47 unproven (was 696 + 31: 16 cases had a cut next header).
- F2, F3 (non-blocking): the lead's journal-tail-cut-header-design quest; no change here.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261121-115925-e0216a |
| A2 | 261121-115925-e0216a, 261121-115928-e3ca87; fails without: 261121-103815-c377f1, 261121-115934-0256e0 |
| A3 | 261121-115931-e97d8e; fails without: 261121-084916-f70fcc, 261121-103756-644f6c |
| A4 | docs/mechanisms/turn-log-damage.md at e912a28 |
| A5 | ci:macos-arm64 |

200-seed T9 at journal:e912a28, identical on macOS (261121-115925-e0216a) and the FreeBSD VM
(261121-115928-e3ca87): 1,228,209 checks, 0 failures; 242,242 crash points and 155,754 simulated
tears report no damage; 18,045 damaged crossing fragments reported (9,632 @4096, 8,413 @2048),
996 more with a cut header only counted (564, 432). CI 44943792157 green at journal:e912a28. The
doc's Evidence paragraph keeps the journal:421011a counts; the landed code stays journal:e912a28.

## Result

- Rule (docs/mechanisms/turn-log-damage.md): a sector is proven by a later verifying fragment
  whose header it holds whole, by the fragment's own header naming segment and epoch after a
  writer's type and reserved byte, or by a whole header of the fragment's write starting before it
  in the sector. A crossing fragment whose first sector is proven, with a verifying fragment of the
  segment whose header the next sector holds whole, is damage.
- Code: `verifies_in_sector` (whole headers only), `damaged_fragment(..., bool written)`;
  scan_segment tracks where the last same-write header starts (`in_write`).
- T9 adds `crossing_damage`, `simulated_tears`, `stale_record_tears`, `cut_header_tears`.

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
- (Superseded by F1.) The next-sector scan used `verifies_in_sector` with cut headers counted,
  assuming ~2^-32 exposure; the review showed a cut header leaves 1-3 checksum bytes (2^-8k).
- F1 fix: the whole header, not only its checksum, must lie in the sector (the review's stronger
  option). It matches the stated rule "only a header the sector holds whole", and the 4-byte-CRC
  variant would still let a sector be proven by 4 checksum bytes with no segment binding.
- The mutant drops only the next-sector line, so it restores the tear reading exactly.
