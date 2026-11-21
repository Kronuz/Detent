# journal-power-silent-loss review: durability

Commit: journal:b1bf19a19c7a594b06d1a6823955b65707f9b761 (base journal:178030b)
Verdict: APPROVE
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261120-201511-64af67 (macOS, dbab658) and 261120-203145-d7fbda (FreeBSD VM, b1bf19a): 200 seeds,
  6 geometries, 1,036,264 checks, 0 failures. Every crash point (241,462, a third with a second cut
  during recovery, each followed by a reopen and 5 more turns) asserts `!damaged && lost.empty()`
  and the acknowledged model by value (turn_log_power_test.cc:117-118). That is the false-positive
  check for both fixes under the sector crash model, rollover and recycling included at 512.
- 261120-201510-15f224: the five named seeds pass at dbab658.
- 261120-201407-d594b9 (4b9166f, no fix) fails seed 91 @512; at 512 the sector end is the block end,
  so fix 2 cannot matter there and the job isolates fix 1. 261120-201509-96e66d (e77d17b, fix 1
  only) fails seed 145 @2048: it isolates fix 2.
- CI run 44865755870: success at b1bf19a (head SHA checked with `gh run view`), every platform job.

## What I checked myself

Scripts in `.scratch/detent/review-journal-power-silent-loss-durability/` (probe.cc replaces the T9
test in a dedicated slot; run.sh runs every geometry, run1.sh one geometry). Each case flips bytes in
a durable 60-turn log, reopens with every group held open, and checks the damage is reported and
every group not lost keeps its acknowledged records by value.

| Probe | What | b1bf19a (261120-212732-0aad27) | 178030b (261120-213315-944786 @2048, -213316-8cf503 @512) |
|---|---|---|---|
| P2 | a byte of block 0's header, every segment | 659 cases, 0 failures | 0 |
| P3 | cross-segment 'both': whole fragment of a segment's last block + next segment's block 0 header | 539, 0 | 3 kept short @2048 |
| P4 | length low byte (6 masks) and high byte (2 masks) of every fragment whole in its sector, newest segment's last two blocks | 5,968, 0 | 68 silent @2048 (of 1,808) |
| P5 | 'last'-style byte in every segment's tail block, log opened three times | 659, 0 | 0 |
| P6 | real tears: every sector-crossing fragment with its later sectors zeroed, random, or stale; `damaged_fragment` called directly | 9,720, 0 read as damage | 0 |
| P7 | worst-case `damaged_fragment` (32 KiB block, 488 trials) | 17.6 us per call | n/a |
| P1 | crossing fragment spanning two sectors, byte in its second sector, a later fragment starting and verifying there | mid-log 2,564, 0; tail 334, 175 unreported, 180 kept short | identical @2048 |

Answers to the lead's questions:

1. Fix 1 (turn_log.h:636) reads a tail as a gap only when its scan set `Item::damage`. That flag
   needs a sector that holds bytes bound to this segment and fails, which a crash does not leave.
   The one exception is fix 2's checksum trial, at about 1.1e-7 per torn crossing fragment. So an
   unwritten or torn tail is still free. The crash points and P6 back this up.
   - Across rollover and reopen: P3 and P5 pass. P3 also shows fix 1 covering a cross-segment
     boundary that the base missed.
   - Clean shutdowns and normal power cuts report no damage at any of the 241,462 crash points.
2. Fix 2 never accepts a record. A match returns true from `damaged_fragment` (turn_log.h:182-189),
   which only sets `Item::damage`, so the fragment stays a gap and no data is invented. A real tear
   matches only on a CRC-32C collision. Each trial uses a different length (w < L), so no algebraic
   collision with the true message is possible. That leaves at most 488 (= 512 - 24) trials at about
   2^-32 each, which P6 does not contradict.
   - Cost on recovery: a trial runs only in a block whose header verifies for this segment, with a
     failing fragment of its segment and epoch that crosses its sector, at most once per block. A
     crash has one or two such blocks. Even a 64 MiB segment damaged in every 32 KiB block costs
     2,048 x 17.6 us = 36 ms.
3. Siblings: block 0 headers (P2), length bytes in every fragment kind and position (P4), and
   cross-segment and multi-epoch tails (P3, P5) are covered. The crossing-fragment case (P1) is
   not: see F1.
4. Evidence: values are checked (per-group record equality), and each fix has its own fails-without
   job. T9 exercises fix 2 thinly (seeds 145 and 199 of 200): see F3.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | A byte flipped in the second sector of a fragment that spans two sectors reads as a tear when that fragment is in the newest segment's tail. The acknowledged last turn is dropped. In about half the tail cases this is unreported, and in 5 cases damage is reported while a group not called lost is short a record. The same counts appear at the base, so this was not introduced. It is the R7 limit (journal-double-damage), but R7's "telling more needs per-sector checksums" is not true here. When a later fragment of this segment starts and verifies in that second sector, the sector is provably new, so both sectors are new and the failure is damage: the same argument `damaged_fragment` already applies to its first sector. Outside T9's patterns ('last' picks only fragments whole in their sector), so outside this quest. | P1: 261120-212732-0aad27 (2048: 59 of 110 unreported; 4096: 116 of 224), 261120-213307-184b44 (the 5 reported-but-short cases at 2048, e.g. seed 8 segment 4 block 1: group 3 acked 11..16, got 11..15), 261120-213315-944786 (identical at 178030b) | Owner's call: a follow-up quest in which `damaged_fragment` also checks, for a fragment that spans exactly two sectors, whether a fragment of this segment verifies in the second sector, with a T9 pattern like P1. Or narrow R7's text to the cases that really need a format change. Recommendation: the follow-up quest, since it costs one more `verifies_in_sector` call. |
| F2 | no | nit | A false positive from fix 2 (a real torn tail that happens to verify at a shorter length) puts the damage in the newest tail, and that block's manifest sits in the unwritten next header. So the cost is every group (`unknown`), not "costs groups" as the journal's ruling says. The probability is still about 1.1e-7 per torn crossing fragment, and nothing is invented. | turn_log.h:686-688 (the `hidden` loop calls `gap_damage`, and a missing manifest sets `unknown`) | State it in the turn_log.h comment or the CHANGELOG entry. |
| F3 | no | gap | Only the two seeds that failed exercise fix 2 in the suite: 'last' is one case per seed and geometry, and a length byte is hit in roughly 1 of 100. P4 found 68 silent cases at the base among 1,808 at 2048. A pattern that sweeps the length bytes of the last turn's whole fragments would pin fix 2 far more firmly than two seeds do. | P4 at 178030b vs b1bf19a | Optional: add a P4-style pattern to T9's `boundary_damage`. |
| F4 | no | nit | test/turn_log_power_vm.sh is outside the quest's file set (`campaign status` warns). | campaign status | The lead widens the file set or drops it from the branch. |

## Not run

- Manifest bytes in a header larger than one sector (half-block manifests at 2048 and up): only
  P2 and T9's 'header' pattern reach manifest bytes, and only within the first sector.
- Damage to fragment headers' segment or epoch fields (the R7 case proper), and fragments spanning
  three or more sectors.
- The geometries 1000 and 768 for P1-P5 (R8: unaligned headers); FreeBSD and Linux for my probes,
  which ran on macOS only.
- Sanitizers for the new trial loop beyond what CI's asan-ubsan run of the suite covers.
