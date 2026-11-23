# journal-tail-second-sector review: durability

Commit: journal:421011acfb9a98bf27b11eb857e18f947911ee76 (quest/detent/journal-tail-second-sector, 3 commits on main)
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261121-103735-cf5964 (macOS) and 261121-103744-ab7d1e (FreeBSD VM), T9 with 200 seeds at
  421011a: identical per-geometry lines, 1,228,757 checks, 0 failures. @4096 there are 9,820
  crossing cases asserted (376 more with a cut header, only counted) and 86,982 simulated tears.
  @2048 there are 8,545 (300) and 68,772. Every crash point asserts `!damaged && lost.empty()`
  and the acknowledged model by value. This is the false-positive check under the sector model.
- 261121-103815-c377f1: `--fails-without` at journal:ba19ae0, where segment and epoch alone prove
  a header again. The three `stale_record_tears` cases fail, so the writer-type ruling has a live
  test.
- 261121-103756-644f6c: `--fails-without` at journal:b9e8c5f, with the next-sector line removed.
  It fails seed 3 @4096 on "the damage is reported", so A3's mutant is live.
- CI 44936652808: success at head SHA 421011a (`gh run view`), every platform job.

## What I checked myself

Scripts are in `.scratch/detent/review-journal-tail-second-sector-durability/`.
`newest_tail_probe.cc` runs through `run.sh SEEDS`, and `cut_proof_probe.cc` runs through `run2.sh`.

| Probe | What | Result |
|---|---|---|
| Q1 newest tail after restarts, 261121-115044-fd4e5e | 24 seeds @2048 and @4096. The log is opened three times: open 1 ends in a power cut mid-turn (an older epoch's torn tail), open 2 closes clean, and open 3 writes 0, 7, 14 or 21 turns and then the newest turn W. W started a segment in 12 runs. (a) For each subset of W's changed sectors (all of them when W changed at most 10, else 1,024 random), those sectors are left at their pre-W bytes, stale recycled bytes included, and the log is recovered with `TurnLog::read`. (b) With every sector of W new, one byte is flipped in each later sector of each W fragment that crosses a sector, 3 offsets each, and the outcome is classified by shape. | (a) 442 tears, 0 damage or loss. (b) Proven shape (span 2, a fragment after it in its second sector, header uncut or `written`): 174 of 174 reported, groups kept by value. Silent, with W dropped unreported: 36 of 210, all in the documented limits. 3 had a cut header and were not `written`. 15 had no later fragment in the second sector (6 of those were W's own last fragment). 18 spanned 3 sectors. |
| Q2 cut proof, 261121-113126-b4537e (exit 1 = defect shown) | F starts after the block header and ends at byte 1023. G starts at 1023, so only G's first CRC byte is in sector 1. Tear: sector 0 new, sector 1 old (zeros, last byte v), sector 2 new. `damaged_fragment` is called over all 256 values of v. A same-sector variant follows: an older write's end-of-turn, then F whole in sector 0, then G at 511. | Next-sector rule: 1 of 256 old values reads the tear as damage, namely the value equal to G's CRC byte (0xf0). Same-sector rule 1 (already on main): also 1 of 256. |

Risk answers:

1. A tear can be read as damage. The new rule does not do it through `written`: in a tear,
   whatever precedes the fragment in an old sector belongs to the last durable write, and that
   write ends with its end-of-turn or open record, so `written` stays false (turn_log.h:244).
   It does not do it through `named` either, except for the documented stale-payload Limit,
   which is practically nil and reported rather than silent (see F3). The real path is the
   proof itself. `verifies_in_sector` accepts a fragment whose header the sector boundary cuts,
   so 1 to 3 CRC bytes "prove" the next sector. A stale byte that equals that CRC byte turns a
   real tear into damage (F1). The failure is loud: in the newest tail it sets `damaged`, and
   `gap_damage` can cost every group (turn_log.h:730).
2. In a crossing fragment's second sector, damage that the rule cannot prove still drops the
   acknowledged newest turn without a report. In Q1(b) that happened in 36 of 210 newest-tail
   cases. The cut-header case (3 of 210) cannot be told from a tear under the sector model:
   the fragment's first sector may be old while the next is new, and only the fragment's own
   head bytes there could tell the two apart. The other 33 are the "last fragment there" and
   "fills the next sector" limits (F2).
3. `written` stays correct. `in_write` is local to each segment's scan. It resets to 0 at every
   block header that verifies, and it is read only after such a header in the same block. It
   becomes nullopt after the last fragment of an end-of-turn or open record. A segment's first
   write starts at a block header, and the first turn after an open follows the open record,
   which ends its write. Q1 covers all three: segment rollover (12 runs), a reopen after a power
   cut, and W as the first turn of its open. It found 0 tears read as damage and no missed
   proven case.
4. The change stays off both paths. The diff touches only `damaged_fragment` and
   `scan_segment`'s `in_write`, not `recover()`. `git merge-tree` of 421011a with
   quest/xapiand/journal-turn-buffers is clean. With quest/xapiand/journal-lost-bound, only
   CHANGELOG.md conflicts, and turn_log.h auto-merges. turn-buffers keeps the fragment bytes and
   one write per turn (framing.h diff), so the model's "one write per turn" premise holds after
   it lands.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | The next-sector proof accepts a fragment whose header the sector boundary cuts. `verifies_in_sector` scans to the sector's last byte (turn_log.h:158), so a fragment G that starts in the last k ≤ 3 bytes of the next sector proves that sector with k CRC bytes. Take a tear where F's first sector is new (`named`), the next sector is old and the one after it is new: it reads as damage when the stale byte equals G's CRC byte, which is 2^-8k and not the ~2^-32 the journal's ruling assumes. This contradicts the rule the doc states (turn-log-damage.md:27, "only through a header the sector holds whole"). The new rule (turn_log.h:189) adds this exposure. Rule 1 (turn_log.h:179) and `damaged_header` (turn_log.h:169) share it on main. The result is a false damage report in the newest tail, so groups are marked lost on a log that is valid. It is rare, about 1e-6 per crossing fragment in a torn write, but it is a stated property that the code does not hold. | Q2 261121-113126-b4537e: 1 of 256 for the next-sector rule and 1 of 256 for rule 1 | `verifies_in_sector` should accept only a fragment whose header the sector holds whole (`p + fragment_header <= end`). The weaker alternative is to require its CRC whole (`p + 4 <= end`), which gives 2^-32. Test: Q2's next-sector shape as a T9 case (a stale byte equal to G's CRC byte is a tear). T9's `crossing_damage` then counts cases where the next fragment's header is cut as unproven, as it already does for a cut header of F. |
| F2 | no | gap | A1 reads "no acknowledged turn is dropped unreported", but in the newest tail the change reports only the proven shape. In Q1, 36 of 210 newest-tail crossing damages still drop the acknowledged last turn silently, against T9's "31 of 727" framing over all blocks. Most of them are the turn's own last fragment, or a fragment spanning 3 sectors. The quest's premise that a verifying fragment in the second sector proves "the sector was written after the damaged one's predecessor, so it is damage" proves only the second sector. The contributor narrowed the claim correctly and documented every residual under Limits. Q1 found no case outside them. | Q1 261121-115044-fd4e5e tally | Lead's call: accept A1 with the Limits as written, or open a format follow-up. One option is a writer that never lets a write's first fragment header straddle a sector, which closes the cut-header case. Per-sector checksums would close all three. |
| F3 | no | nit | The Limit's argument (turn-log-damage.md:71-75) covers stale (turn, group, sequence) and manifest pairs, but not a record's sequence followed by its payload's first 8 bytes. That pair is preceded by the group id, which the host chooses and which can be any u64 (host.h mixes ids, it does not bound them). A group with byte 6 in 1 to 4 and byte 7 zero, a sequence equal to the new segment number, and a payload that starts with the epoch matches the pattern. It fails loud and is very unlikely. | file:line above | One sentence in the Limit naming group ids. |

## Not run

- Q1 at 512, 1000, 768 or 256-byte blocks: no fragment crosses a sector inside those blocks, and
  T9 covers their crash points.
- Linux and FreeBSD runs of Q1 and Q2: both are deterministic predicate and model checks. T9's
  FreeBSD job already matches macOS.
- Fragments that start mid-sector in unaligned blocks (1000, 768): already outside the model
  (Limits).
- No measurement of recovery cost: the new rule adds at most one `verifies_in_sector` call per
  failing fragment.
