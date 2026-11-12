# wheel-lib: journal

State: waiting on jobs 261111-233420-9245bb 261111-233422-3a6c0b 261111-233426-cba5bf
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

- Exp 4 result, probe 261111-221538-088160 (R = 3, n = 1,000): forwarders default w 1.169x a
  (A/A 13.3%; arm 6.54 vs 5.03, re-arm 10.75 vs 7.95, cancel 5.28 vs 4.98, expire 22.13 vs
  20.30); forwarders inlined 1.145x (A/A 23.7%; arm 5.98 vs 3.89, re-arm 6.56 vs 5.42, cancel
  4.96 vs 4.59, expire 21.92 vs 20.72). A1 rows at wheel:94a51c6: single 261111-221548-54b209
  misses at n = 1,000 (1.123x, A/A 24.8%; 1.097x at 1M), cross 261111-221548-4b10b6 passes.
- Exp 5, the last (wheel:1cd7e7a, after merging main's .gitignore): arm no longer calls
  cancel()+file(); it unlinks or reads queued(), files a level-0 deadline inline (a passed one at
  the cursor via max), and sends a far deadline or a waiting inbox to one noinline arm_slow.
  Probe 261111-232553-4ce4d5 (R = 3, n = 1,000):
  - forwarders inlined: w 1.076x a (A/A 8.9%); arm 5.26 vs 5.24, re-arm 10.57 vs 11.65,
    cancel 4.80 vs 5.04, expire 24.09 vs 19.77. The arm path is closed; expire is the gap.
  - forwarders default (A1's build): w 1.176x (A/A 7.4%); arm 6.08 vs 4.86, re-arm 8.86 vs
    7.73, cancel 5.27 vs 4.83, expire 23.83 vs 19.91. cand_w::arm stays out of line: clang's
    inline cost 285 vs threshold 225 (local -Rpass-missed; job 261111-233446-2a29ab), 270-285
    in three further unlink variants tried at compile time only; the one call arm keeps plus the
    queued() test and the far branch are what a lacks.
  - n = 1,000,000: inlined 1.004x, default 1.242x (A/A 16.7%; R = 3).

- At wheel:1cd7e7a: suite green 261111-233424-51ff26; CI green 261111-233426-fc722e (run
  43175199669); mutants killed 261111-233426-1629a0 (no cascade), -739727 (no owner assertion),
  261111-233427-f38b25 (next() unsaturated), -8621ae (model fix); inline cost probe
  261111-233446-2a29ab: cand_a::arm 205 (inlined, threshold 225), cand_w::arm 285 (not inlined).

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

1. Read the A1 rows at wheel:1cd7e7a: single 261111-233420-9245bb (R = 10), cross
   261111-233422-3a6c0b, and the fuzz rerun 261111-233426-cba5bf (A5). The rest passed (above).
2. Write the numbers into docs/timer-wheel.md Cost and README Performance (A6), the Evidence
   table, Result; State ready for review, or the A1 miss stated plainly for the lead.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending: 261111-221548-54b209 (single), 261111-221548-4b10b6 (cross), at wheel:94a51c6 |
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
