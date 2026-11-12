# wheel-lib: journal

State: working
Quest: .campaign/detent/quests/wheel-lib.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1, the gap is inlining, not placement:
- Placement ruled out: probe 261111-202423-339389 (exclusive, pads 0-61440 B, n = 1,000): w is
  1.36-1.50x a at every pad, heads and entries at the same addresses mod 64 KiB in both arms;
  every pass slower (arm 6.5 vs 3.9 ns, re-arm 9.5 vs 5.1, cancel 5.5 vs 4.3, expire 22.5 vs 18.0).
- Cause: probe 261111-205658-e77a03 (nm of the -O2 bench): timer_wheel::arm, cancel and file were
  out of line; the prototype's are inlined.
- Exp 1 (wheel:b669137): arm = cancel + file, drain and the upper-level filing out of line. Probe
  261111-210424-5bd3a9: cancel and file now inline, arm still out of line.
- Exp 2 (wheel:2b1ee3a): `[[gnu::always_inline]]` arm and cancel. nm probe 261111-210835-78d9cf,
  exclusive A/B a vs w R = 3: 261111-210839-efd0f6 (superseded exp 1's 261111-210437-53c405).

Correctness so far: suite green at wheel:01dfcc9 (261111-204407-aef4ec); mutants caught: no
cascade 261111-202912-b5aee7, no owner assertion 261111-202915-2b286a, unsaturated next()
261111-204408-d59cd1. CI green at wheel:4a5bef4 (run 43159140943).
Bugs found and fixed:
- next() overflow for the last tick before never (CI, wheel_test far): wheel:01dfcc9.
- Model bug: a fire's due remote arm filed by a later fire (libFuzzer 261111-202944-9750f6): the
  input is fuzz/corpus/, replayed by ctest wheel_fuzz_corpus; mutant 261111-210559-9425eb.
- cross test stopped before slow producers' far arms were due (CI macOS arm64, FreeBSD):
  wheel:4a5bef4.
Fuzz rerun 261111-210604-65353d.

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
