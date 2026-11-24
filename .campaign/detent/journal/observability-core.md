# observability-core: journal

State: design ready for re-review
Quest: .campaign/detent/quests/observability-core.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design round 3 (re-review CHANGES at Detent:0e096e0e, review file at Detent:e4218ae2; lead ruled
segment bitmaps): docs/design/observability.md rewritten for marks + fold + rows-for-counts; the
model tests/detent_reader/usage_claim_model.cc rewritten for marks, the fold and the row claim. No
core code. Model job queued (see Evidence).

## Next

1. Whole-design re-review at max effort. Owner (through the lead): `ever_read` in
   detent_knob_usage (D6) and DETENT_MINIMUM_MAPPING_BYTES 1,055,136 → 1,071,520 (D10).
2. Before any code edit in src/detent_local, the lead widens the file set to D10's surface:
   src/detent_local/paged_page_access.hh, lazy_page_access.hh, lazy_page_access.cc,
   paged_read_state.hh, paged_read_state.cc (evaluation.hh is already in), plus view.cc,
   tests/detent_local/test_evaluation.cc, tests/detent/test_one_knob_differential.cc (`.value`).
3. Then the code per the Build plan's first row; A2 per D2's gate and Checks.

## Findings → fixes (design round 3)

| Finding | Fix (docs/design/observability.md) | Check |
|---|---|---|
| Lead ruling P2 | D10: segment bitmaps, one bit per 8 B beside VerifiedPages; claim index moved to Alternatives; rows kept for sampled counts only (D3 Rows) | design lens |
| R1 fold price and surface | D3 "The fold" (walk, carry, budget 1,024 keys per freer pass, ≤1 pending per namespace, runs on the `refreshing` holder, never a warm lookup) and "Price of the fold" table: 100K/1M/5M × R = 1,000/4,096/every knob (76-141 ms, 0.76-1.4 s, 3.8-7.0 s for every knob; estimates, measured in A2); memory: carried names + one pending generation within the cap. D10 "Surface in src/detent_local" (files above) and "Mapping cap" (bitmap charged to the account; minimum +16,384 B, core.cc:59-60 static_assert) | model: `fold` scenarios, `budget_and_pending`, `stress`; A2 fold timing (Checks) |
| R2 G1/overflow | D2 G1 re-derived: warm hit 0 marks, 1 RMW per sample only, ≤1.5 probes per sample; `usage_slots` 256 case ≤2.5 probes per sample, samples in the overflow row; `overflowed` now means counts only, ever-read exact at any slots (D6 items 1-2, Limit 3) | model: `overflow_counts`; Lifecycle `overflow`, `gate_events` |
| R3 G2 at 10 ns | D2 G2: threshold = D48's ns / that case's own off median (5 ns at 1 thread, 10 ns at 8 and 64); strict pass is a pass; strict that does not pass every case, or with A/A > half a threshold, decides nothing → one exclusive run (D135) | lead rules |
| R4 cycle | D2 Sampling item 3: reload uniform in [1,127] from per-stripe xorshift; test asserts ≥512 of 1,024 names within 64 cycles and all within 1,000 (fixed 64 gives 16) | model: `cycle_coverage` over 100 seeds; Lifecycle `cycle` |
| R5 boundary | D3 Rows item 4: a full claimer re-examines the slot it saw empty before setting the flag | model: `boundary`; Lifecycle `claim_boundary` |
| R6 fresh handles | Lifecycle `fresh_handle`: 10,000 handles, 30-160 sampled (78.7 expected) | model: `fresh_handles` |
| R7 T4 | D10: mark bitmaps fall under T4's per-segment bitmap clause; rows and carried names are core heap, reported against 1.5 MiB | design lens |
| R8 callback re-entry | Lifecycle `callback_reentry` row; D6 item 3 collects under `refreshing`, runs callbacks with no lock | Lifecycle |
| P4 table rebuild + class sweep | Lifecycle rebuilt for marks/fold (states: off/on; attached, retired pinned, retired, pending, freed); the fold's class listed under the table (retire while pinned, fold in budget, deleted key, failed successor lookup, past budget, second retire while pending, two into one successor, beside marking readers, refresh/usage finishing, fork child, close), each with its row and test | design lens |

## Findings → fixes (design round 1)
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

Lead: whole-design re-review at max effort; file set widened to D10's src/detent_local surface.
Owner (through the lead): `ever_read` (D6) and the new DETENT_MINIMUM_MAPPING_BYTES (D10).

## Messages

- msg-1 (lead: file set widened; view records in; P2/P3 to the owner): done. View records are in
  D5 and D6; the file set still lacks view.cc and the two evaluation tests (N7).
- Coordinator's design fix round (CHANGES, F1-F8): done, table above.
- Coordinator: re-review CHANGES at 0e096e0e, lead rules segment bitmaps, R1-R5 and the table
  rebuild: done, round-3 table above.
- Coordinator: D158 accepts D6's ABI shape; recorded in the doc's header and D6. P2 (D10) is
  presented with both options, the corrected numbers and a recommendation.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |

Model (round 3): job `261124-112726-6e7cf6` (light, TSan, at Detent:fa7e5fba) exit 0, no TSan
report, all eleven outcomes as designed; cycle: at least 615 of 1,024 names in 64 cycles and all
within 800 over 100 seeds (fixed 64: 16); fresh: 69 of 10,000 first lookups sampled.

## Result

<Not yet.>

## Rulings I made

1. Rows keyed by full name and published once from a private row: no merge, no wait, no duplicate.
2. (Round 3, replaces old 2-4) Marks are the read record; rows hold sampled counts only, and any
   sampled status in an opened namespace takes a row by name (missing included), so `ever_read`
   separates "read OK" from "has a row".
3. The fold's budget is 1,024 keys per freer pass, at most one pending generation per namespace;
   a second retire finishes the pending one unbudgeted.
4. A failed successor lookup during the fold carries the name rather than retrying.
5. Fork child remaps rows, name set and mark bitmaps anonymous, clears carried and pending (N3) and reseeds stripes.
6. `usage_slots` maximum 8,388,608: above the store's 5M knobs, below allocations that cannot
   succeed.
7. Mark bitmaps are charged to the mapping cap with their segment; the minimum grows by 16,384 B
   (owner's, reserved ABI constant).
