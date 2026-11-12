# wheel-vs-stash: journal

State: ready for review
Quest: .campaign/detent/quests/wheel-vs-stash.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done. Harness over the fetched wheel (wheel:bfd72bc) and stash (stash:b7d887678a); arms b, b2 (A/A
twin), w. Test 261112-131222-a47299 (ASan+UBSan, TSan, both binaries) passed at 43f5dbf1; the rows
ran at 2e69b9f16b3c: single 261112-134519-339b0d, cross 261112-134522-786aad, niche (24 h, N = 1e3
and 1e6) 261112-134524-cd4fc1 (all exclusive), mem 261112-134528-c24e96 (light). All exit 0, every
run valid. Summaries re-run from the logs with the harness's scripts and the wheel's
wheel_bench_summary.py match the logs' own. Host: Intel i9-9980HK, macOS, clang 17. Result in
docs/perf/wheel-vs-stash.md.

## Next

1. Review (low risk, performance assignment).

## Blocked on

Nothing blocking. For the lead: `wheel.hh` (the prototype) stays because the async-drivers
design-record tests and probes (loops.hh, wheel_test, placement_test, driver_cost, ...) include it;
they are not wheel-vs-stash measurements. A3 is read as "the wheel-vs-stash and stash-niche
targets build no copy" (as D83 keeps wheel-evidence.md).

Findings for wheel-followups: the wheel's worst fire lateness is 3.4 ms (P = 4) and 4.5 ms (P = 14)
against stash's 1.02 ms (0.20 ms at P = 1), cause not isolated; and 1,000 fire-and-forget keys at
P = 1 and 4 cost the wheel 2.3x and 1.8x stash's total (wake-per-key drain, about 2 us per wake).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261112-131222-a47299 (passed at 43f5dbf16f78; ignores docs-only docs/design/async-drivers-bench/wheel_vs_stash_common.sh), 261112-134519-339b0d, 261112-134522-786aad, 261112-134524-cd4fc1, 261112-134528-c24e96 |
| A2 | `docs/perf/wheel-vs-stash.md`: "Apples to apples, per row" |
| A3 | `docs/perf/wheel-vs-stash.md`: "The targets build the fetched library, no copy and no candidate arm" |

## Result

docs/perf/wheel-vs-stash.md: the landed wheel against stash (w/b median, 95% interval, A/A band).
Whole life of a timer 0.037x at n = 1e3 and 0.32x at 1e6 (42 vs 1,110 ns; 407 vs 1,231 ns);
cross-thread arm 0.097x, 0.061x, 0.103x at P = 1, 4, 14 (14.8, 12.1, 31.8 vs 148, 199, 307 ns);
24 h keys at N = 1e6 cost 0.071-0.095x (211-239 vs 2,237-3,315 ns), memory 32.07 vs 382.21 bytes
per key. Stash wins: cancel (flag clear, 0.66 vs 3.69 ns; not the same work), fire lateness at
P = 4 and 14, N = 1e3 niche at P = 1 and 4 (wheel 2.3x, 1.8x), empty footprint (9,248 vs 65,552
bytes). Open: the lateness and the N = 1e3 drain causes are not isolated. Linux not measured.

## Rulings I made

1. Stash arm is (b) only; the intrusive-leaf candidate (c) is removed with its header (D83: no candidate arm).
2. wheel_vs_stash_vm.sh removed: it staged the evidence-era copies; Linux is a later milestone check.
