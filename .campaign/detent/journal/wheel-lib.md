# wheel-lib: journal

State: working
Quest: .campaign/detent/quests/wheel-lib.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Moved task's "// Timers" block (task:5a1e516) into wheel.hh (wheel:c92c2c3); tests, model, fuzz,
demo committed. Suite 261111-202844-8575e6 queued. A1 baseline rows at wheel:c92c2c3 queued
(exclusive): single 261111-202415-05333c, cross 261111-202419-8b74a9; pads probe (a, w)
261111-202423-339389.

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
