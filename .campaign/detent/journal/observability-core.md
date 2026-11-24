# observability-core: journal

State: design ready for re-review
Quest: .campaign/detent/quests/observability-core.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design fix round for review `observability-core-design.md` (Detent:549a81d8, CHANGES at
Detent:1fbd4cd9) is done: docs/design/observability.md rewritten as one design (P1-P6 folded into
D1-D10, Lifecycle, Limits, Build plan), and the claim protocol modelled in
tests/detent_reader/usage_claim_model.cc (run by usage_claim_model.sh under TSan). No core code.

## Next

1. Design re-review; the owner rules D10's mechanism ((a) claim index or (b) segment bitmaps).
   D6's ABI shape is accepted (D158).
2. Then the code per the Build plan's first row: evaluate's arm, rows + claim index + countdowns +
   totals + view records in core.cc, `detent_usage` in reader.h/reader.hh/exports.list, testing.hh
   seams, test_reader_usage.cc (one test per Lifecycle row), reader_bench `--usage-gate`.
3. A2: G1 (in the suite), G2 strict at head, base/head reader_bench against main.

## Findings → fixes (design round 1)

| Finding | Fix (docs/design/observability.md unless named) | Check |
|---|---|---|
| F1 returned read invisible | D3 "A knob's mark": row (or flag) published before the index address (release, then acq_rel CAS, read with acquire); a sampled hit with an unstored row `find`s it; Visibility item | model: `F1 visibility` parked at AddressClaimed and at RowWritten; Lifecycle `claim_windows` |
| F2 row claim unspecified | D3 "A row's life": private row written, published once by CAS into an insert-only name set; full-name match; failed CAS re-examines the slot; spares for losers; full: flag + overflow row stored in the index slot; orderings stated; slot 16 B, index 128 KB | model: `F2 two claimers` at three windows (no wait, one row, spare reused), `equal hashes`, `stress` (16 threads, exact sums); Lifecycle `claim_windows`, `equal_hashes` |
| F3 bound is names | D3 "Fallbacks take no row": only OK reads claim; fallbacks add to an existing row and exact stripe totals; consequence stated (Limit 10) | model: `F3 missing names take no row`; Lifecycle `fallbacks` |
| F4 memory comparison | D10: table at 100K/1M/5M for R = 1,000/4,096/every knob, both sides with rows, T4 budget, crossovers, (b)'s walk and fold; recommendation (a) at the default, (b) if Q7's every-definition exactness holds | from the stated sizes (row 288 B, set 4 B, slot 16 B, 16 KB per segment, 63 B per knob) |
| F5 table vs code | "Lifecycle": rebuilt from core.cc states and events, each with a test that fails without it; `kept_generation` uses `Stats::claims`, `fresh_handle` counts first-lookup samples over 1,000 handles; D5 view states failed and dropped | design lens |
| F6 gate undecidable | D2 "The gate": G1 exact event counts (any level); G2 on/off ratio with an off/off A/A, valid at ±0.5%, ceilings 1% (1 thread) and 2% (8, 64), plus a 256-slot case past the bound | lead rules A2's level |
| F7 two designs | Whole doc rewritten: 0 is the default everywhere, `DETENT_USAGE_OFF` is off; positions, bitsets, fold, hash keys, `wrong_type`, old Limits 1/2/11/12, OB1/OB4 criteria and the abstract replaced | design lens |
| F8 open header items | D6: maximum 8,388,608 (INVALID_ARGUMENT above), ABI version stays 1, `attached_ns` CLOCK_MONOTONIC via clock_gettime, off-handle results, callback rules, totals struct | design lens |
| N1 | D4: `switch_forced` not derivable; OB4 decides | |
| N2 | Limit 8 | |
| N3 | D3 "Fork": anonymous mmaps remapped in the child, no copy | Lifecycle `fork_child` |
| N4 | Limit 3 (churn) | |
| N5 | D6 totals: statuses, publishes, refused, attached; stale answers shown by `current − seen`. Lead: in scope or not | |
| N6 | D4 last paragraph | |
| N7 | Lead widened the file set (msg-1); view.cc, test_evaluation.cc, test_one_knob_differential.cc still needed for `.value` | lead |
| N8 | D6 item 1: independent relaxed cells, not a snapshot | |

## Blocked on

Lead: design re-review; A2's run level (D2 proposes G1 plus strict G2); N5 and N7 scope.
Owner (through the lead): D10's mechanism (recommend (a); (b) if Q7 holds for every definition).
D6's shape is accepted (D158).

## Messages

- msg-1 (lead: file set widened; view records in; P2/P3 to the owner): done. View records are in
  D5 and D6; the file set still lacks view.cc and the two evaluation tests (N7).
- Coordinator's design fix round (CHANGES, F1-F8): done, table above.
- Coordinator: D158 accepts D6's ABI shape; recorded in the doc's header and D6. P2 (D10) is
  presented with both options, the corrected numbers and a recommendation.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |

Model: local TSan run under `quiet light` (all outcomes as designed); model job below.

## Result

<Not yet.>

## Rulings I made

1. Rows keyed by full name and published once from a private row: no merge, no wait, no duplicate.
2. Rows persist by name across publishes, which replaces D10's fold.
3. The claim index is sized at attach to 2 × min(usage_slots, root.records), so small namespaces
   do not pay 4,096 slots.
4. Fallback lookups take no row (F3 option b); their exact counts go to per-stripe totals.
5. Fork child remaps rows, name set and indexes anonymous (N3) and reseeds stripes.
6. `usage_slots` maximum 8,388,608: above the store's 5M knobs, below allocations that cannot
   succeed.
