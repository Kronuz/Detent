# delta-plan-warm: journal

State: ready for review
Quest: .campaign/detent/quests/delta-plan-warm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done: results in docs/perf/delta-plan-scaling.md "A warm second plan on one mapping".

## Next

1. Review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Exclusive `--for A1` rounds at Detent:f6145feb (harness Detent:39155f00): 500K 261119-153419-124e73 (5 rounds), 5M 261119-153422-107aa3, 261119-153424-6e0a4a, 261119-153426-bc9994. Warm same delta over cold, wall: 500K 0.667/0.804/0.781 (uniform 0.05, 0.5, clustered 0.05), 5M 0.660/0.760/0.790. Smoke 261119-152539-acabbc. |
| A2 | `docs/perf/delta-plan-scaling.md`, "A warm second plan on one mapping". |

## Result

A second delta plan on a mapping the first plan touched takes 0.66 of the cold wall time at 0.05 uniform (5M:
12.10 s to 8.14 s, CPU 10.11 s to 8.14 s; 500K: 1.110 s to 0.735 s), 0.76-0.80 at 0.5 (5M 24.73 s to 19.45 s)
and 0.78-0.79 clustered, at both sizes, with non-overlapping ranges. The 5M cold plan spends about 2.1 s of
wall time outside the CPU (faults), which the warm plan does not. A different uniform delta on the warm
mapping plans like the same one. Open: a first plan after a reboot (cold OS cache) is not measured; the
bitmap cannot be separated from the process's warm state. No source changed; the harness is tests only.

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
- The harness lives under the file set's `tests/detent_local/test_delta_plan_scaling_warm.*` and includes
  docs/perf/namespace-measure-t9.cc as a unit (main renamed), instead of editing the T9 harness; it is not
  registered with ctest.
- 500K corpus derived from the 5M one (local number below 500,000), as the 1M one was.
- Rounds: 5 at 500K, 3 at 5M (10-minute cap per exclusive job); exclusive jobs ran alone (no `ran beside` line).
- The clustered "different delta" is omitted from the table: A and B rewrite different bytes (12 and 6 MB).
