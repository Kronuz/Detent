# journal-tail-cut-header-design: journal

State: waiting on jobs 261123-061004-47129e 261123-062108-d42575
Quest: .campaign/detent/quests/journal-tail-cut-header-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Doc at journal:HEAD of the quest branch (not pushed): blocks rows, T9 rows, journal main d8453f6
(turn_log.h unchanged since a3eecb5, checked). Two placeholders remain: BLOCKS_EXPLAIN and
LIMIT_SCOPE. Finding: the blocks rows show silent loss *before* the clean log's newest turn (layout 0
23 of 4807, l1 16 of 4429, l2 11 of 2988; O3 model 0). Hypothesis from the code: an unproven
failure `break`s out of its block (turn_log.h:263-267), so every later turn of that block is unread,
and the damaged turn becomes the newest epoch's torn tail (turn_log.h:833): silent loss reaches back
to the damaged turn, wider than turn-log-damage.md's Limit ("in the last turn").

## Next

1. Read 261123-061004-47129e (journal:dba8a7c, prints each earlier-turn silent case: segment,
   newest or older, writes after it). If every case is in the newest segment's last block with later
   writes after it, fill BLOCKS_EXPLAIN with that and LIMIT_SCOPE with "the Limit's text must say
   every turn from the damaged one to its block's end, not the last turn" (a Q for the lead: the
   mechanism doc is not this quest's file set). Else state what it shows.
2. Read 261123-062108-d42575 (A2 at the quest head, prototype as `--ref PROTO`): counts must equal
   cd9e33/8b1268/5f7c2a. Trim the doc to about 2000 words, revision pass, push, paste the brief's
   Evidence table, Result, "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/turn-log-cut-header.md` on quest/detent/journal-tail-cut-header-design (journal) |
| A2 | pending: 261123-062108-d42575 (l0-l2 at the quest head, prototype as a ref). Supporting, at the prototype: 261122-211721-cd9e33 (l0), 261122-211725-8b1268 (l1), 261122-211728-5f7c2a (l2), 261122-212211-6e741f (l0 at journal main), 261122-211558-e05832 (T9 l1, 0 failures), 261122-211602-21b060 (T9 l2, 13 failures) |

Jobs examined, the first round (all at journal:e0e9c37 or 9b4a5a5):

| Job | Result | Verdict |
|---|---|---|
| 261121-132915-ad7e2d probe l0 | exit 1, 1 failure: "W changed no sector of an existing segment: seed 20 block 4096" | harness: at seed 20 (0 turns in open 3) W opens a new segment, outside the shape; now counted and skipped (journal:da29e7f). Counts unaffected: the run returned before counting |
| 261121-132624-b0f43f probe l1 | same single failure | harness, as above |
| 261121-132629-eaefca probe l2 | same single failure | harness, as above |
| 261121-132644-3269e2 T9 l1 | 25 failures, every sweep 0 failures | as designed: 2 stale-record fixtures sit at layout 0's offsets, and 23 "G verifies with k bytes of G's header before the boundary" preconditions fail because layout 1 never places G's header across the boundary. Fixtures now run at layout 0 only; the probe checks layout 1's placement instead |
| 261121-132652-7dd3f8 T9 l2 | 16 failures: 3 fixtures as above; 12 "a corrupted block loses only groups with records in it" / "every other group keeps" in segments 4-5 at 2048 and 4096; 1 crossing-fragment silent loss (seed 28, 4096, segment 5 block 0 offset 3850) | finding about the layout-2 prototype, not yet attributed: rerun with T9_DEBUG (261122-211602-21b060). O2 is rejected on bytes alone |
| 261121-132716-d4b057 turn_bench, exclusive | passed | the doc's latency table |

Second round: cd9e33, 8b1268, 5f7c2a match the doc's newest counts; 6e741f equals cd9e33. T9 l2
(21b060): 12 failures where a corrupted block at 2048/4096 charges 1-2 groups with no record in it
(loud over-charge), 1 at seed 28, 4096, segment 5 block 0 offset 3850 where damage is reported but
group 5 keeps 2 of 4 acked records unnamed; not traced further (O2 rejected on bytes and closure).

Superseded and cancelled before running: 261122-211543-7dd050, 261122-211547-f80c5b,
261122-211550-60ab98, 261122-211554-1510be, 261122-211732-99dde2.

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The prototype is one commit with layouts behind `JOURNAL_LAYOUT`, built per layout, so every
  option runs the same probe source at one named commit; layout 0 is journal:e912a28's code path.
- O3's closure is a model (a damaged sector equals none of its versions), its bytes are derived
  from layout 0's measured fragments, and its write cost is an emulation (layout 3): building the
  format is the follow-up quest, not this design's.
- The O1 header-only check that would close F3 is analysed, not prototyped.
- journal main moved to journal:a3eecb5 (journal-lost-bound) after journal-tail-second-sector's
  8b063a1; `damaged_header` and `damaged_fragment` are byte-identical at e912a28, 8b063a1 and a3eecb5,
  so the layouts stay on the e912a28 prototype; layout 0's probe also runs at a3eecb5 to show it.
- A2's evidence must run at the quest's code (`campaign brief` rejects jobs at the prototype
  commit), and the probe lives outside the file set (docs/design/**): one job at the quest head
  builds layouts 0-2 from the prototype passed as `--ref PROTO`, rather than moving the probe
  into the quest branch.
- The blocks rows tally the clean log's newest turn apart: the doc's claim that only the newest
  tail is silent is checked, not assumed.
