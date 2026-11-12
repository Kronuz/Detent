# wheel-vs-stash: journal

State: Held by the owner until the logger-on-wheel decision; resume from here
Quest: .campaign/detent/quests/wheel-vs-stash.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Held (lead's message: owner paused the quest until the wheel stabilizes; no measurement job was
submitted). Done so far, committed as WIP (the harness does not build at this commit):

- CMakeLists.txt: `-DBENCH_WHEEL_VS_STASH=ON` builds only wheel_vs_stash and stash_niche, with
  FetchContent wheel at wheel:bfd72bc13395e4a232701e752be97ccd236530fc and stash at b7d887678a
  (scheduler d57e8fc's pin; stash.h identical to d249352).
- Removed wheel_vs_stash_leaf.hh (candidate c), stash_niche_wheel.hh (copy of wheel:50665c8) and
  wheel_vs_stash_vm.sh (staged the copies and candidates).
- Not yet done: wheel_vs_stash.cc and stash_niche.cc still include the removed headers.

## Next

1. Rewrite wheel_vs_stash.cc to arms b (stash as is), b2 (A/A twin), w (wheel::timer_wheel,
   `arm_remote` for cross-thread); keep single/cross/mem/test and the workload; add ghz0/ghz1.
   Drop a, a2, c, d, e, f and `#include "wheel.hh"` of the copy (it resolves to the fetched one).
2. stash_niche.cc: reuse cand_w from wheel_vs_stash.cc, drop its copy; arms b, b2, w; 24 h rows.
3. Scripts: wheel_vs_stash.sh / stash_niche.sh build through cmake (-DBENCH_WHEEL_VS_STASH=ON,
   CMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG"; sanitizer and COUNT_HEAP in own build dirs), print
   `round=R` before every sample as Kronuz/wheel's wheel_bench.sh does (40 rounds, rotated arm order).
4. Summaries: load `_deps/wheel-src/bench/wheel_bench_summary.py` by path (importlib) and call its
   parse/summarize with ref b, twin b2 for single and cross; niche and mem summaries use its
   median_interval, stash the reference.
5. Jobs: test (ASan+UBSan, TSan) and smoke first; exclusive rows at most 10 minutes each (single
   1e3 and 1e6 split; cross; niche 24 h at N = 1e3 and 1e6 split by rounds); mem.
6. docs/perf/wheel-vs-stash.md: tables, job ids, one apples-to-apples line per row (arm, and the
   whole life score: yes; re-arm, cancel, expire per operation: no, stash's cancel and re-arm leave
   stale filings its expire pays for; cross-thread arm: yes; memory: entry plus heap per armed entry),
   and "Harness".

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
