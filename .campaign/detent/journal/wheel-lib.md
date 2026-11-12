# wheel-lib: journal

State: working
Quest: .campaign/detent/quests/wheel-lib.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Finding (probe 261111-202423-339389, exclusive, pads 0-61440 B, a vs w at n = 1,000): w is
1.36-1.50x a at every pad, with heads and entries at the same addresses mod 64 KiB for both
arms. Placement is ruled out; the gap is code. Every pass is slower, not only re-arm: arm 6.5
vs 3.9 ns, re-arm 9.5 vs 5.1, cancel 5.5 vs 4.3, expire 22.5 vs 18.0. Hypothesis: unlink/file
outlined at -O2 (a call per unlink fits all four deltas); disassembly probe 261111-205658-e77a03
(the earlier 261111-203618-617a41 cancelled for it).

CI green at wheel:01dfcc9 (run 43156701805: ASan+UBSan, TSan, Linux/macOS/FreeBSD); A4 job
261111-204535-5b0ffc checks that run passed at the commit.

CI caught a real bug (run 43155780729, wheel_test far, deadline never - 1): next() overflowed
computing the end of the last tick before never and wrapped to a passed time. Fixed at
wheel:01dfcc9 (saturate at never); mutant job 261111-204408-d59cd1 restores the old line.
Suite at head 261111-204407-aef4ec.

## Next

1. Suite green; mutants (no cascade, no owner assertion); ASan+UBSan and TSan jobs; fuzz job.
2. Read A1 rows; if w misses 1.10x at n = 1,000, probe the cause (placement pads; code alignment,
   the i9-9980HK's JCC erratum) and try layouts, at most five experiments.
3. Docs: README, ARCHITECTURE, AGENTS, CHANGELOG, docs/timer-wheel.md with the A1 numbers.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |

## Result

Pending.

## Rulings I made

- Names: `wheel::timer_entry` and `wheel::timer_wheel`, task's names under the library's
  namespace, so task's later integration is a namespace change.
- C++20 (`<bit>`'s countr_zero and rotr), as task and logger already require.
- The harness is task's bench/wheel port (Detent:5ecbeb4c workload verbatim), arm `t` renamed
  `w` (this library). `WHEEL_BENCH_GATE=w` makes the job exit 1 when w's median ratio passes
  1.10x in any row, so an A1 job passes only on the bound; `WHEEL_BENCH_CXXFLAGS` is for probes.
