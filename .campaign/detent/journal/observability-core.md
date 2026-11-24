# observability-core: journal

State: blocked on lead: design check of the lifecycle table (docs/design/observability.md P1-P6), the P2 ruling (owner's: narrows Q7) and the file-set extension (P6)
Quest: .campaign/detent/quests/observability-core.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: waiting on the lead's design check of "In the paged core" (docs/design/observability.md,
sections P1-P6). No code is written yet (contributor rule: mechanism → lifecycle table first).

## Next

1. On approval: `evaluate` returns `{value, arm}` (P1; needs evaluation.hh in the file set).
2. core.cc: usage table (rows by name), stripe countdowns (64 lines, xorshift reload [1,127]), claim
   index per `Generation` (allocated at attach), overflow row and flag, fork-child clear and reseed,
   view records (attach time); `detent_usage` and `usage_slots` in reader.h, `Reader::usage()` in
   reader.hh, exports.list; testing.hh seam to force the period to 1.
3. tests/detent_reader/test_reader_usage.cc: one test per P4 row (names in the table).
4. reader_bench usage-interleave mode (P5; needs benchmarks/reader_bench.cc in the file set); A2
   jobs: the gate (strict) and the base/head comparison against main (strict).
5. `campaign verify observability-core` (A3), push, ready for review.

## Blocked on

Lead (and owner where noted), with recommendations:

1. **Design check** of P4's lifecycle table and P2's mechanism (`design` lens).
2. **P2, owner's (D47/D48/D61 narrow):** the landed paged core has no dense definition position, so
   D10's bitmap cannot be built in core.cc. Options: (a) per-generation claim index keyed by body
   address, rows by name, exact up to `usage_slots` knobs with the overflow stated (core.cc only,
   memory ∝ knobs read); (b) D10's bitmaps on segment locations: changes paged_read_state and
   PageTable, ~4.9 MB per generation at 5M, and a verified-page walk to name the bits.
   Recommend (a).
3. **P3, owner's (C ABI reserved):** the exact declarations; `usage_slots = 0` means the default
   (4,096) like `mapping_bytes`, off is `DETENT_USAGE_OFF`. The design said `0` is off; recommend
   the default convention. Arms: `switch_forced` replaces `wrong_type` (D65).
4. **Scope:** the quest numbers OB2 as ever-read bits and OB3 as `detent_usage`; the design's OB2 is
   view records. Recommend including D5's view records in `detent_usage` (D6's accepted shape;
   cheap: one clock read at attach). Hook stays out.
5. **File set (P6):** add `src/detent_local/evaluation.hh`, its callers' one-line `.value` changes
   (`src/detent_local/view.cc`, `tests/detent_local/test_evaluation.cc`,
   `tests/detent/test_one_knob_differential.cc`) and `benchmarks/reader_bench.cc`.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |

## Result

<Not yet.>

## Rulings I made

1. Rows keyed by full name (compare on claim) instead of D3's 64-bit hash: claims run once per
   knob per generation, so the compare is off the warm path and Limit 1 (silent merge) goes.
2. Rows persist by name across publishes, which replaces D10's fold (no bit-to-name mapping).
3. The claim index is sized at attach to `min(usage_slots, root.records)` × 2, so many small
   namespaces do not each pay 4,096 slots.
4. Fork child clears rows and indexes (its set is its own reads, D7 item 4) and reseeds stripes.
