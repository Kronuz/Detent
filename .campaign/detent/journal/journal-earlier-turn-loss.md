# journal-earlier-turn-loss: journal

State: waiting on jobs 261123-072804-625133 261123-072807-0eb04a 261123-072809-40f146 261123-072811-92707d
Quest: .campaign/detent/quests/journal-earlier-turn-loss.md (the lead's; never edit it)

## Doing

Jobs queued at journal:e0a47a3 (A1, A2, A3) and journal:fca82bb (A1 fails-without); branch pushed
for CI (`ci:ci`, suite).

## Next

1. When the jobs finish: paste `campaign brief`'s Evidence table, set State "ready for review"
   (the durability review is the lead's to launch).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261123-072804-625133 (fix), 261123-072807-0eb04a (fails-without at fca82bb) |
| A2 | pending: 261123-072809-40f146 |
| A3 | pending: 261123-072811-92707d; CI suite on push |
| A4 | `docs/mechanisms/turn-log-damage.md`: "Damage to the newest turn ... reads as a torn tail ... **The newest turn drops silently**: 36 of 201 such cases in the cut-header probe. Only a per-sector checksum (the cut-header design's O3b, a format change) tells this damage from a tear. Damage to any earlier turn is reported (rule 6), and costs the groups rules 3 and 5 name: every group when the damaged block is the newest, whose manifest no later header holds yet." |
| A5 | pending: CI at e0a47a3 |

Local, not evidence (journal-wt build/): at main the new test fails 30 checks (seeds 4, 6 at
4096 B, 22 at 2048 B: U1, U2, U3, same block and later block; the large header); at the fix 317
checks, 0 failures. Probe at the fix: blocks 0 SILENT of 4807 (all "reported"), newest tail 36 of
201 unchanged, 0 of 427 tears misread, blocks-newest-turn 29 of 108 unchanged, 4619 checks 0
failures.

## Result

Cause confirmed (A1 test at main): an unproven failure ends its block's scan
(`scan_segment`), the later turns in that block go unread, and the damaged turn reads as the
newest epoch's torn tail. A second shape the probe's seed 6 shows: when the gap swallows the
damaged turn's first fragment, only the newest (incomplete) turn is visible after it, and the
break at the torn tail hid the skipped turn numbers.

Fix (journal:921301d, code only, RFTW0001 unchanged): rule 6. A write starts only after every
earlier write's barrier returned, so a record of a later turn than a failure belongs to makes it
damage.
- `later_turn` reads the rest of a failing block for fragments of the segment that verify and
  keeps the highest turn their records name (`Item::later`); for an unproven fragment failure,
  and for a failing header only in a gap's first block.
- `recover`, past the newest epoch's last kept turn: a gap whose failure belongs to turn >= T (the
  first not kept, raised by each record before it) is damage when a record after it, or its own
  `later`, names a turn > T. Rule 5 then costs it.

Recovery cost: TurnLog::read of a 64 MiB segment (default geometry, 200 turns), strict, 3
interleaved rounds of 15 reads: medians main 19.4/18.1/19.3 ms, fix 19.0/19.1/18.2 ms (segment
1); main 18.4/17.7/18.4, fix 19.5/17.5/18.2 ms (segment 256). Searching every unwritten block cost
600 ms for segment 256 (memchr on a zero byte) and ~2 ms otherwise, hence the anchor and the
first-block rule.

Docs: `docs/mechanisms/turn-log-damage.md` (rule 6, the Limit, Evidence), `CHANGELOG.md`.
Leaves open: the newest turn's 36 of 201 (design O3b, a format change).

## Rulings I made

- Proof only, no mid-block resync: the records past a failure are evidence, never recovered
  data. Resyncing would parse at unaligned positions, where a payload could forge a record; and
  every record they could add is costed anyway (the damaged block's manifest, or `unknown` in the
  newest block).
- The comparison lives in `recover`, not `scan_segment`: it needs the turn context across
  segments (a segment's first write) and covers the later-block case through the same rule.
- Only the newest epoch, as rule 5: an earlier epoch's next open names what it kept; an epoch
  followed by an inferred open needs its open record damaged too (a second fault).
- A failing header after a gap is not searched: a later write there needs a second fault (the
  first failure is damage or a tear of the newest write). This keeps unwritten blocks free.
- A2's probe ref is journal:b335b69 (journal main a3eecb5 plus `test/cut_header_probe.cc`, the
  design's job 261122-212211-6e741f), not e69ce03, which holds only the design doc: the probe
  file is not in e69ce03. turn_log.h and framing.h are identical between a3eecb5 and main
  d8453f6. The job builds the probe tree as is (main) and this tree plus the probe (fix).
- The large-header shape (a manifest of 28+ groups puts the block header past its first sector,
  so no fragment proves it) is in scope: same cause, same rule, its own test and tear guard.
