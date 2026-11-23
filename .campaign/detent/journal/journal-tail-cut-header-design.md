# journal-tail-cut-header-design: journal

State: waiting on jobs 261122-211721-cd9e33 261122-211725-8b1268 261122-211728-5f7c2a 261122-212211-6e741f 261122-211558-e05832 261122-211602-21b060
Quest: .campaign/detent/quests/journal-tail-cut-header-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Doc `docs/design/turn-log-cut-header.md` (journal quest branch, journal:6f39f97) filled except BLOCKS and
the T9 rows; prototype `scratch/cut-header-proto` at journal:8485149 (off e912a28); layout 0 probe at
journal main on `scratch/cut-header-probe-main` at journal:b335b69 (a3eecb5 plus the probe).

## Next

1. Read the six jobs: confirm the l0/l1/l2 newest counts equal the doc's table (36/201, 30/189, 36/159),
   PROBE_MAIN equals l0, fill BLOCKS from `blocks` vs `blocks-newest-turn` rows (the claim: no silent
   case before the newest turn), and the T9 rows (l1 0 failures; l2 debug: root-cause or state it).
2. Trim the doc to about 2000 words (2537 now, tables included), run the revision pass, write
   "design ready".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/design/turn-log-cut-header.md` on quest/detent/journal-tail-cut-header-design (journal) |
| A2 | 261122-211721-cd9e33 (l0), 261122-211725-8b1268 (l1), 261122-211728-5f7c2a (l2), 261122-212211-6e741f (l0 at journal main), 261122-211558-e05832 (T9 l1); pending |

Jobs examined, the first round (all at journal:e0e9c37 or 9b4a5a5):

| Job | Result | Verdict |
|---|---|---|
| 261121-132915-ad7e2d probe l0 | exit 1, 1 failure: "W changed no sector of an existing segment: seed 20 block 4096" | harness: at seed 20 (0 turns in open 3) W opens a new segment, outside the shape; now counted and skipped (journal:da29e7f). Counts unaffected: the run returned before counting |
| 261121-132624-b0f43f probe l1 | same single failure | harness, as above |
| 261121-132629-eaefca probe l2 | same single failure | harness, as above |
| 261121-132644-3269e2 T9 l1 | 25 failures, every sweep 0 failures | as designed: 2 stale-record fixtures sit at layout 0's offsets, and 23 "G verifies with k bytes of G's header before the boundary" preconditions fail because layout 1 never places G's header across the boundary. Fixtures now run at layout 0 only; the probe checks layout 1's placement instead |
| 261121-132652-7dd3f8 T9 l2 | 16 failures: 3 fixtures as above; 12 "a corrupted block loses only groups with records in it" / "every other group keeps" in segments 4-5 at 2048 and 4096; 1 crossing-fragment silent loss (seed 28, 4096, segment 5 block 0 offset 3850) | finding about the layout-2 prototype, not yet attributed: rerun with T9_DEBUG (261122-211602-21b060). O2 is rejected on bytes alone |
| 261121-132716-d4b057 turn_bench, exclusive | passed | the doc's latency table |

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
- The blocks rows tally the clean log's newest turn apart: the doc's claim that only the newest
  tail is silent is checked, not assumed.
