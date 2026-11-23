# journal-tail-cut-header-design review: durability

Commit: journal:9e0649dea76f66e5b7a799a0672df5e3fcd21953 (the design note `docs/design/turn-log-cut-header.md`; first reviewed at journal:76729d17765fbb95b666b18bc6449a36a4bb1d3b)
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Re-review of 76729d1..9e0649d (one commit, the design note only): F1-F4 are fixed in the text, and
no finding blocks. First round: CHANGES on F1. The recommendation itself, O3b over O0, O1 and O2, stands: under the
stated crash model D4's four verdicts are complete in an executable model of D2-D4, and the cost
numbers follow the measurement rules and favour it.

## Evidence I relied on

- 261123-062108-d42575 (A2, quest head journal:35d4dd1 with the prototype journal:dba8a7c as a
  ref): its 115 count lines equal, line for line, those of 261122-211721-cd9e33,
  261122-211725-8b1268 and 261122-211728-5f7c2a (layouts 0-2 at journal:8485149); layout 0 at
  landed main (261122-212211-6e741f, journal:b335b69) equals cd9e33. dba8a7c adds only a print to
  the probe over 8485149. Layouts 0-2 run the real `TurnLog::read`, so their counts predict the
  code (risk 4).
- 261123-061004-47129e: the 23 earlier-turn silent cases, each printed with its segment, fragment
  and later writes.
- 261121-132716-d4b057 (exclusive, journal:9b4a5a5): its log's per-round p50 and turns/s give the
  doc's medians (layout 0, 256 B: 20921, 20902, 20935 us, median 20921; 48.0 turns/s), and the
  0.18 ms round spread the doc states.
- Bytes for O0-O2 are deterministic counts in light jobs at a named commit, which the measurement
  rules allow for bytes.

## What I checked myself

Scripts in `.scratch/detent/review-journal-tail-cut-header-design-durability/` (home checkout):
`o3b_model.py` is an executable model of D2-D4 (sectors of 504 data bytes and an 8-byte trailer,
4-byte fragment headers, whole-sector writes rewriting the partial sector, D4's reader stopping at
the first stale, zero or failing sector it needs). It runs every subset of the newest write's
sectors left old (old = the partial sector's earlier bytes, zeros, or a recycled file's stale
sector), a bit flip in every sector holding acknowledged data at each tear outcome, a recycled
file whose stale sectors come from segment S - 2^32, and, outside the model, an acknowledged
sector that reads back as zeros. `O3B_Q3=1` adds Q3's write-order rule to D4;
`O3B_MUTANT=damage-as-end` must make damage silent, and does (the model is live).
`patch_q3.py` produced the second version from the first; rerun only `o3b_model.py`.

- 261123-072901-f7e1ff, the evidence (200 seeds at 2048 and 4096-byte blocks, one turn in four
  with records up to 4 KiB, 24 runs skipped whose turns overflow the model's segment): D4 as
  written reads 55,069 of 55,069 tears as tears over zeroed files and 55,069 of 55,069 over
  recycled ones, and reports 27,411 of 27,411 flipped bytes in acknowledged sectors in each; with
  the low 32 bits bound, 2,042 of 55,069 tears over an aliased file misread, 0 with all 64 bound.
  D4 plus Q3: tears unchanged (55,069 of 55,069); reverts reported 21,381 of 27,411. Mutant:
  1,003 of 1,003 damages silent, so the checks are live.
- 261123-070035-1c7066, an earlier version of the script (24 seeds, records up to 700 B): the same
  shape, 4 of 356 aliased tears misread.
- 261123-070215-fea0f4 failed on the model's own overflow (fixed in the script); not evidence.

Risk by risk:

1. D4's verdicts under the stated model (torn at sector granularity, reordered sectors, bit rot):
   complete. Every tear read as a tear and every flipped byte in an acknowledged sector was
   reported, at 2048 and 4096-byte blocks, over zeroed and recycled files. The reason is that
   only the newest write can leave a sector old, the only sector a later write rewrites (the
   partial one) keeps a zero where the next write starts, and CRC-32C over 4096 bits (data, segment bits,
   index) detects every error of up to 5 bits and every burst of up to 32, so rot never yields a verifying, zero or stale sector. Outside the model
   there are shapes it reads as tears: F3, F4.
2. 32 bits are not enough as D2 binds them, and 64 stored bits are not needed: F1.
3. Cost: measured as the rules require, and it supports O3b (F5 for what it cannot show).
4. Model fidelity: layouts 0-2 are the code; the O3 column is not a model of D4 (F2), which my
   model now covers.
5. Q3 first wastes no format work: Q3 changes no bytes on disk, and O3b's D4 replaces its code.
   Its rule is still worth keeping in D4 (F3), so Q3's tests carry over as shapes, not code.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no (fixed at 9e0649d) | defect | D2's CRC binds only the segment's low 32 bits, so a stale sector of a recycled file written at segment S - 2^32, at the same index, verifies as this segment's and a tear over it reads stale bytes as current (O3b has no fragment CRC behind it). Recycled files are not zeroed (`turn_log.h:640-646`, `666-670`), so a sector no reuse reaches keeps its oldest segment; reaching 2^32 segment numbers takes about 2^32 opens or 256 PiB at 64 MiB segments: remote, but no invariant excludes it, and the cure costs no bytes. | Model, alias rows: 2,042 of 55,069 tears misread with the low 32 bits bound; 0 of 55,069 with all 64 bound (f7e1ff). | D2: the CRC covers the full 64-bit segment, the trailer stores its low 32 bits; the reader checks this segment, then the one 2^32 below with those bits (or the nearest below when the bits differ), and a sector neither verifies is damage. 4 more stored bytes (12-byte trailer) are not needed. The build quest's framing test seals a sector at S - 2^32 and asserts it reads as stale under S. |
| F2 | no (fixed at 9e0649d) | gap | The O3 column is true by construction: the probe flags any changed byte (`test/cut_header_probe.cc:119-122` at journal:dba8a7c) and runs no O3 reader, so "0 of 201" and "0 tears read as damage" for O3 are not evidence for D4. | Probe source; my model is the D4 evidence. | Cite an executable D4 model (this one, or the build quest's T9 rewrite asserting every shape, step 2 of the plan) beside the 0 of 201. |
| F3 | no (fixed at 9e0649d) | gap | Outside the stated model, an acknowledged sector that reads back as zeros (a lost or remapped write), or as its older version, takes D4's "never written" or "end" verdict: the segment ends there and every acknowledged turn after it drops without a report. Q3's write-order rule, kept in D4 (a later turn read from a later block makes the end damage), reports most of them and misreads no tear. | Model, revert rows: 27,411 of 27,411 silent under D4; 6,030 silent and 21,381 reported under D4 plus Q3, which still reads 55,069 of 55,069 tears as tears (f7e1ff). | Name it in Limits; carry Q3's rule into D4, so journal-earlier-turn-loss's shapes stay its tests under O3b. |
| F4 | no (fixed at 9e0649d) | gap | D4 gives no verdict for an unreadable sector (EIO), the way most drives report rot; today a short block read is a gap without damage (`turn_log.h:244`), a silent tear in the newest tail. | Code reading. | D4: an unreadable sector is damage. |
| F5 | no | nit | O3b's bytes are derived from layout 0's turns, not an O3 emitter's (labelled as such), and the latency row cannot see CPU cost below macOS's 21 ms barrier (stated). The decision does not hinge on either: O1 closes 6 of 36 at +0.15%, O2 none net at +28-31%, O3b all in the model at -5.6 to -6.2% (+1.0% at 4 KiB records). | d4b057, cd9e33. | The build quest measures the emitter's bytes and a strict Linux row against main. |

F6 and F7 are from the re-review at 9e0649d.

## Re-review at 9e0649d

- F1: D2 now stores the low 32 bits and binds all 64 in the CRC; the reader checks this segment,
  then the nearest lower segment with the stored bits (2^32 below when they match). This is the
  rule `o3b_model.py`'s `verdict` runs with `full64` (the two candidates), so f7e1ff's 0 of 55,069
  misread tears are its evidence. Is the nearest lower segment enough? An older reuse can never
  verify as current: a sector from S_old reads as this segment only if its CRC verifies under S,
  which needs S_old = S or a CRC collision. A sector more than 2^32 segments old reconstructs to
  S_old + k * 2^32, fails both checks and reads as damage: loud, never silent (F6).
- F2: the O3 column is now labelled "by construction", and the doc cites f7e1ff for D4.
- F3: D4 keeps Q3's rule. Limits states the reverted-sector shape with 27,411 / 21,381 / 6,030, and
  Interactions says journal-earlier-turn-loss's shapes stay its tests.
- F4: D4 counts an unreadable sector as damage.

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F6 | no | nit | A stale sector more than 2^32 segments older than this one (a recycled file's sector that no reuse reached in that span) fails both checks and reads as damage, so a tear over it is reported as damage. It is loud and needs about 2^32 opens or 256 PiB written; Limits does not name it. | D2 at 9e0649d; the model's `verdict`. | One Limits line. |
| F7 | no | nit | D2's "one that verifies under neither is damage" can be read as making an all-zero sector damage. The O3 verdict table (all zeros: never written) says otherwise. | D2 and the O3 table at 9e0649d. | D2: "...under neither, and is not all zeros, is damage". |

## Not run

- No O3 code exists, so nothing here is evidence about an implementation: the model is a model.
- Tears inside a sector, CRC collisions and damage past the newest write's end (the doc's Limits)
  were not modelled; nor were rules 3-5's group costs, which D4 leaves unchanged.
- D5 (strata's manifest log on the sector layer): checked only that a framing stream has one
  segment and one epoch (`docs/mechanisms/framing.md:14`), so dropping the fragment's segment and
  epoch holds there too; strata's own tail rule was not read.
- I am a subagent of the lead's session, not a session of my own, so no `detent · reviewer`
  session title applies.
