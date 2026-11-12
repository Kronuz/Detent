# wheel-vs-stash: journal

State: waiting on jobs 261112-131849-2dcaf3 261112-131851-01ae87 261112-131854-396348 261112-131856-04408f
Quest: .campaign/detent/quests/wheel-vs-stash.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness rewritten over the fetched wheel (wheel:bfd72bc) and stash (b7d887678a); arms b, b2 (A/A
twin), w. Passed at 43f5dbf1-era harness: test 261112-131222-a47299 (ASan+UBSan, TSan, both
binaries; model and cross checks), smoke 261112-131756-4c2758. Submitted at the harness commit
(all with --ref wheel, stash pinned): exclusive single 261112-131849-2dcaf3 (n = 1e3, 1e6, 40
rounds), cross 261112-131851-01ae87 (P = 1, 4, 14, 40 rounds), niche 261112-131854-396348 (24 h at
N = 1e3 40 rounds, 1e6 24 rounds), light mem 261112-131856-04408f (wheel_vs_stash mem, nichemem at
1e3 and 1e6). Host: Intel i9-9980HK, macOS, clang 17.

## Next

1. When the jobs finish: read each job's log summary; if one failed or hit its bound, say why and
   rerun (split rounds).
2. docs/perf/wheel-vs-stash.md: tables (w and b medians, w/b ratio with 95% interval, A/A band),
   job ids, one apples-to-apples line per row (arm: yes; re-arm, cancel, expire per operation: no,
   stash leaves stale filings its expire pays for, whole-life score: yes; cross-thread arm: yes;
   niche: yes, same keys; memory: entry plus heap), and "Harness" (fetched library, no copy; the
   wheel.hh in this directory serves the design-record tests only).
3. Journal Evidence/Result, State "ready for review".

## Blocked on

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
