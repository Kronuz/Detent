# journal-double-damage review: durability

Commit: journal:cca242714125a1edd5a94fed75d23c389a8e72e6
Verdict: CHANGES
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1: 261120-094327-db2bf3 (passes at cca2427), 261120-094322-5b34a1 (fails at 920a4f5). Accepted.
- A2: 261120-094329-72ecca, 261120-094325-3140b2: 295 boundaries, each a byte past block k's
  header plus one byte in block k+1's header, all reported, no kept group short. Accepted for that
  pattern at block_bytes 512 only. At 512 a block is one sector, so the sector argument holds
  trivially there.
- Suite: 261120-094331-af0f45. Accepted.

## What I checked myself

The scripts are in `.scratch/detent/review-journal-double-damage-durability/`:

- `probe.cc` is the power test plus a probe, run by `run.sh`.
- `debug.sh` lists each group that was kept short.

Probe jobs:

- 261120-100617-a6b0db: the probe at the fix.
- 261120-100620-f37d5e: the same probe at 920a4f5, before the fix.
- 261120-101042-682539: a single header flip at block_bytes 512, listing each kept group that
  misses records.

What the probe runs:

- **P1, power loss alone.** The power test's crash model at block_bytes 512, 4096, 2048, 1000, 768
  and 256. That covers multi-sector blocks, blocks that are not sector-aligned and sub-sector
  blocks.
- **P2, damage patterns.** Each pattern is applied at every block boundary. Each case is counted
  as reported, as lost while reported (a kept group misses an acknowledged record), or as lost
  silently.

Answers to the named risks:

1. **Crash alone.** P1 at the fix: 9624 crash points over the six geometries, 0 failures. Power
   loss never read as damage. The code argument also holds:
   - Every open starts a fresh segment number, and numbers are never reused (`create_exclusive`
     takes `next_segment_++`, and recycling renames to a new number), so old bytes never carry
     this segment's number.
   - The header and the candidate fragment start share one sector, so a crash leaves both old or
     both new.

   The rule is reachable by tearing finer than 512 bytes, for example pmem/DAX without BTT. That
   is outside the crash model framing.h:45 declares. Damage the rule misses is covered by F2 and F3.
2. **R3.** Damage that no manifest names cost every group. No kept group was short in any
   geometry: 0 "lost while reported" for A2's pattern, against 5 at 920a4f5 with block_bytes 512.
   The new path that names damage past the last kept turn by the block's manifest does keep short
   groups (F1).
3. **Other torn-tail readings.** A single payload byte flipped in the last written block is lost
   silently in 26 of 48 cases, before and after the fix (F4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A single damaged header past the last kept turn is now reported, but a group recovery keeps can still miss an acknowledged record. The dropped incomplete turn spans several blocks. `items[i].damage` costs only the groups that the damaged block's manifest names (found in the next header), at turn_log.h:654. The turn's readable records in its other blocks are dropped as the torn tail, and their groups are kept short. Example: seed 1, segment 34, block 2. Recovery stops at last_turn 59 of 60, and group 3 is kept with 3 of 4 records. Counts of short kept groups by block_bytes: 8 of 295 at 512, 41 of 847 at 256, 1 to 2 elsewhere. At 920a4f5 the same cases were silent; the fix makes them reported, but they still lose data. | 261120-100617-a6b0db (P2 "header byte k+1 only"), 261120-101042-682539 | Once a damage item shows the tail was acknowledged, do one of two things. Either keep the dropped turn's readable records, as the path for an acknowledged incomplete turn does (turn_log.h:634-643). Or cost every group with a record in the dropped turns. Add a test: the power test's double_damage loop with a header-only flip, checking that every kept group holds all its acknowledged records. |
| F2 | no | gap | Damage the rule misses: the header plus the first fragment's header, or the header zeroed with 32 more bytes. These are silent in 21 of 295 cases at 512, 61 of 847 at 256, and 5 to 16 at the other geometries. At block_bytes ≤ 512 the whole block is one sector, so any fragment of this segment that verifies anywhere in that sector proves the sector is new. That would cover more cases under the same argument. | 261120-100617-a6b0db (P2 patterns 2 and 3) | Options: scan every offset in the header's sector for a fragment of this segment that verifies, or record the limit next to R2. |
| F3 | no | gap | When block_bytes is not a multiple of 512, block starts fall mid-sector, so the rule cannot see many headers. At block_bytes 1000, A2's own pattern is still silent in 8 of 170 cases, and a single header flip in 6 of 170. valid_block_bytes accepts any value from 256 to 65559. | 261120-100617-a6b0db (block 1000) | Options, the owner's call: require block_bytes to be a multiple of 512 or a divisor of 512, document the limit, or keep it as is. |
| F4 | no | gap | A single damaged payload byte in the last turn still reads as a torn tail: 26 of 48 cases, unchanged from 920a4f5. turn_log_power_test documents this. With a block of one sector or less, a fragment that fails while a verified header shares its sector is damage, by R2's own argument. | 261120-100617-a6b0db (P2 pattern 5) | Outside this quest. Options: a follow-up quest, or keep the documented limit. |
| F5 | no | nit | The suite's crash model runs the new rule only at block_bytes 512, where the sector argument is trivial. P1 shows it holds for multi-sector and non-aligned blocks, but nothing in the suite keeps that true. | turn_log_power_test.cc:82 | Run the crash points at a second geometry, such as 4096 or 1000. |

## Not run

- A header with more than about 28 manifest entries, longer than one sector (the R2 limit): not
  probed.
- Byte-granular tearing (pmem/DAX): reasoned about only.
- A missing or truncated newest segment file, and damage to an earlier epoch's open record: not
  probed.
- No sanitizers and no Linux run; CI covers those.
