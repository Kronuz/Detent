# wheel-vs-stash: journal

State: working
Quest: .campaign/detent/quests/wheel-vs-stash.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Hold lifted (D86). Harness rewritten to the fetched wheel (wheel:bfd72bc) and stash (b7d887678a):
arms b, b2 (A/A twin), w in wheel_vs_stash.cc and stash_niche.cc; ghz0/ghz1 per row; scripts build
through cmake (wheel_vs_stash_common.sh), print `round=R` before every sample, 40 rounds by default;
summaries load the wheel's bench/wheel_bench_summary.py by path (statistics) and print w/b and b2/b
ratios with 95% intervals. Syntax-checked; test and smoke jobs next, then the exclusive rows.

## Next

1. Jobs at the commit: test (both binaries, ASan+UBSan, TSan), smoke (strict).
2. Exclusive rows, each at most 10 minutes: single (1e3, 1e6), cross (P = 1, 4, 14), niche 24 h at
   N = 1e3 and 1e6 (rounds split if over 10 minutes), mem (wheel_vs_stash, nichemem).
3. docs/perf/wheel-vs-stash.md: tables, job ids, one apples-to-apples line per row, "Harness".

## ## Blocked on

Nothing blocking. For the lead, A3: `wheel.hh` (the prototype) stays because the async-drivers
design-record tests and probes (loops.hh, wheel_test, placement_test, driver_cost, ...) include it;
they are not wheel-vs-stash measurements. Recommend keeping them as the design record (as D83 keeps
wheel-evidence.md), with A3 read as "the wheel-vs-stash and stash-niche targets build no copy".

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. Stash arm is (b) only; the intrusive-leaf candidate (c) is removed with its header (D83: no candidate arm).
2. wheel_vs_stash_vm.sh removed: it staged the evidence-era copies; Linux is a later milestone check.
