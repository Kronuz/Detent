# wheel-lib: journal

State: waiting on jobs 261111-215314-98348e 261111-215326-330b4d 261111-215326-76eefa 261111-215329-6a6e03
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

- Exp 2 result, probe 261111-210839-efd0f6 (R = 3): n = 1,000 w 42.60 vs a 36.83 = 1.157x (from
  1.467x; A/A band 38.7%); cancel now 4.22 vs 4.84; left: arm +1.5 ns, re-arm +1 ns, expire
  +3 ns. n = 1,000,000 1.006x.
- Exp 3 (wheel:f9f3004): expire called the out-of-line drain() every call (its queued() test was
  inside); guard inline at every caller. file(): one unsigned compare for the round (a passed t
  wraps past it). Probe 261111-215314-98348e; A1 rows moved to head: single 261111-215326-330b4d,
  cross 261111-215326-76eefa; suite 261111-215329-6a6e03.
- Exp 3 result, probe 261111-215314-98348e: 1.163x at n = 1,000 (A/A 9.6%), no change: loser 1.
  Disassembly (llvm-objdump of the probe build, 261111-220402-af91be): timer_wheel::arm is inline
  but the harness's own forwarder cand_w::arm is now out of line (3 call sites, the inlined
  body too big for clang); cand_a::arm inlines. Cancel and expire are inline.
- Exp 4 (wheel:94a51c6): one link site in file(), the inbox path in an out-of-line
  drain_and_unlink. Harness gains WHEEL_BENCH_FORWARD (empty by default). Probe
  261111-221538-088160 (bench/forward_probe.sh) runs a vs w with the forwarders left to the
  compiler and then always inlined for every candidate alike.

Correctness so far: suite green at wheel:01dfcc9 (261111-204407-aef4ec); mutants caught: no
cascade 261111-202912-b5aee7, no owner assertion 261111-202915-2b286a, unsaturated next()
261111-204408-d59cd1. CI green at wheel:4a5bef4 (run 43159140943).
Bugs found and fixed:
- next() overflow for the last tick before never (CI, wheel_test far): wheel:01dfcc9.
- Model bug: a fire's due remote arm filed by a later fire (libFuzzer 261111-202944-9750f6): the
  input is fuzz/corpus/, replayed by ctest wheel_fuzz_corpus; mutant 261111-210559-9425eb.
- cross test stopped before slow producers' far arms were due (CI macOS arm64, FreeBSD):
  wheel:4a5bef4.
Fuzz 261111-210604-65353d passed 10 min; mutant 261111-210559-9425eb caught (replay fails
without the model fix). Exp 2 nm probe 261111-210835-78d9cf: arm, cancel, file all inline now.
Suite green at wheel:50665c8 (261111-211937-fea447); CI green at wheel:f729317 (43160670793).
Reruns at wheel:f729317: mutants 261111-213036-87dba7, -aa6c77, -c02379, -f26d0f; fuzz
261111-213036-092ef2; A4 CI check (below).

## Next

1. Read exp 2 (261111-210839-efd0f6) and the A1 rows; if they pass, write the numbers into
   docs/timer-wheel.md Cost and README Performance (A6), Result here, State ready for review.
   If w still misses, experiment 3 of 5 (remaining deltas by pass).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261111-215326-330b4d (single), 261111-215326-76eefa (cross), at wheel:f9f3004 |
| A2 | 261111-211937-fea447 (suite at wheel:50665c8); fails without: 261111-202912-b5aee7 (no cascade), 261111-202915-2b286a (no owner assertion), 261111-204408-d59cd1 (next() unsaturated), 261111-210559-9425eb (model fix); reruns at wheel:f729317 261111-213036-87dba7, -aa6c77, -c02379, -f26d0f |
| A3 | 261111-211937-fea447 (wheel_alloc_test in the suite) |
| A4 | 261111-213050-6af05a (CI run 43160670793 green at wheel:f729317: linux-asan-ubsan, linux-tsan, Linux/macOS/FreeBSD) |
| A5 | 261111-213036-092ef2 (fuzz/fuzz_ops.cc, 10 min, seeded with fuzz/corpus) |
| A6 | pending the A1 numbers in `docs/timer-wheel.md` |
| A7 | after ready for review: the Xapiand lead |

## Result

Pending.

## Rulings I made

- Names: `wheel::timer_entry` and `wheel::timer_wheel`, task's names under the library's
  namespace, so task's later integration is a namespace change.
- C++20 (`<bit>`'s countr_zero and rotr), as task and logger already require.
- `.gitignore` is outside the file set: left as the repository started it; `.scratch/` is
  excluded through the worktree's info/exclude. The lead may want `.scratch/` in .gitignore.
- Local sanitizer jobs cancelled once Kronuz/wheel had a remote: CI runs ASan+UBSan and TSan;
  A4's job checks the CI run passed at the commit (`gh run list --commit`).
- The harness is task's bench/wheel port (Detent:5ecbeb4c workload verbatim), arm `t` renamed
  `w` (this library). `WHEEL_BENCH_GATE=w` makes the job exit 1 when w's median ratio passes
  1.10x in any row, so an A1 job passes only on the bound; `WHEEL_BENCH_CXXFLAGS` is for probes.
