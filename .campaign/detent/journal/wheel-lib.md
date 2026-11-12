# wheel-lib: journal

State: waiting on jobs 261112-053956-3b0fae 261112-054000-50cdce 261112-054004-86840d
Quest: .campaign/detent/quests/wheel-lib.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1 measurement round (lead's assignment: is 45f98eb's owner-side check a cost; can A1 be
measured tightly enough to decide 1.10x).

- Cause of the wide bands: the summary's band was the larger min-max spread of the runs, so one
  outlier run set it and more runs only widened it. Re-summarized offline with paired ratios
  (candidate / reference within a round, median over rounds, 95% order-statistic interval): the
  D75 single row at 45f98eb (261112-045107-d98a70) is w/a 1.213 [1.150-1.410] at n = 1,000 with
  an A/A band of 4.7% (min-max 19.6%): a real miss, not noise; every pass ~1.2x. At 1cd7e7a
  (261111-234624-f5cc8e) 1.054 [1.002-1.095], band 6.2%. The cross row's P = 4 and 14 bands stay
  wide (42%, 57%, R = 5).
- Layout: f (unchanged code) is 0.933x a in the 1cd7e7a build and 1.133x in the 45f98eb build;
  its re-arm 0.62x vs 1.35x, a's own re-arm 11.8 vs 6.4 ns. Builds move loops up to 2x on this
  CPU (i9-9980HK, Skylake-derived: the JCC-erratum microcode).
- Harness wheel:27718c2 (bench only): paired summary, R = 40 default, ghz per sample, load and
  CPU speed limit per round, producer CPU time and spread, `ab` mode, disasm.sh.
- Disassembly 261112-053944-8a557a: at 45f98eb the armed path (re-arm, cancel) is 5 instructions
  shorter (unlink's one compare); the unarmed arm adds a load of e.next (pprev's line) and one
  not-taken jne: clang splits `queued() | (e.next != nullptr)` into two branches.
- wheel:ec5439a: AGENTS describes the paired method (docs only).
- Jobs: Q1 A/B (w at f540679 vs head, a from both builds, plain and -mbranches-within-32B-
  boundaries) 261112-053956-3b0fae; single row R = 40 261112-054000-50cdce; cross row R = 40
  261112-054004-86840d.


Review round at wheel:45f98eb (correctness CHANGES F1, N1-N3; A7 Xapiand CHANGES F1, F3, F4, F6).

- wheel:b822ac5 answers correctness F1, N1, N2, N3 and A7 F1. Option 1, owner side: an unarmed
  e with `e.next != nullptr` is queued behind another push, so arm/cancel call
  drain_and_unlink, which drains every non-empty stripe (`drain(true)`: a relaxed load per
  stripe, exchange the non-empty ones). Soundness: the owner acts on e only after e's
  arm_remote returned (contract), so it reads e.next as written and, by write-read coherence,
  each stripe head at B's CAS or later; only the owner empties a stripe, and it files what it
  takes, so e is found. An e at the bottom of its stripe (next == nullptr): its own pusher's
  fetch_or is sequenced before arm_remote returns, so the owner's queued() load sees the bit,
  or a later value that only the owner's own drain wrote after taking that stripe (e then
  armed). The remote path is unchanged (no RMW added). Hot path: arm/cancel of an unarmed e
  read e.next (same line as pprev) beside queued(). Test: `wheel_test shared` holds A between
  CAS and bit with the WHEEL_TEST_PUSHED hook (empty unless defined), finds a thread on A's
  stripe, pushes B, then the owner cancels / arms B. N1 remains, documented in
  docs/timer-wheel.md (Threading contracts): expire/next() see B's entry once A's bit is set,
  bounded by A's next instruction plus its caller's wake; only shared stripes. N2: one unsigned
  compare on uintptr_t, not std::less (no <functional>; same shape as file()'s round test).
  N3: arm's comment reworded.
- wheel:45f98eb answers A7 F6, F3, F4. Destructor detaches every armed entry to the due list
  (the `detach` helper expire now shares), fires each `false` once, then drains and unlinks
  whatever those fires armed or queued, unfired: bounded. F3: header and docs say a false fire
  means the wheel is dying, the entry is dead, never armed or cancelled on it again. F4:
  reentry mode `destroy` (b's fire deletes b), one line in docs/timer-wheel.md and AGENTS.
- A7 F2, F5: Xapiand's side, skipped as directed.

Jobs at wheel:45f98eb: suite 261112-045015-68d268 green. Mutants killed, each by the right CHECK
(logs): F1 cancel bits-only 261112-045016-03086f (a=1 b=1 c=1), F1 arm bits-only
261112-045020-d35b49 (a=0 b=0 c=0 size=3, as the reviewer's probe), F1 drain bits-only
261112-045024-2acb3a, F6 old destructor 261112-045028-3630df (second dying fire). Old mutants
rerun and killed: 261112-045042-c739f7 (no cascade), -045044-6131c7 (owner), -045047-01cc0b
(next()), -045050-1426c6 (model fix). Fuzz 261112-045053-06a547 passed (830,956 runs, 601 s).
ASan+UBSan suite 261112-045105-3b0736 green. CI 261112-045138-2d2840 green (run 43221778598).
Pending, exclusive: A1 D75 single 261112-045107-d98a70, cross 261112-045109-600561,
default-build single 261112-045112-1213da.

Earlier rounds:

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

1. Read 261112-053956-3b0fae (Q1: w.new/w.old and a.new/a.old, plain and JCC-aligned),
   261112-054000-50cdce (single R = 40) and 261112-054004-86840d (cross R = 40; is producer_cpu_ns
   tighter than producer_ns at P = 4, 14; ghz and speed_limit per run). Queued behind another
   quest's light ASan hold until about 06:24. Then write the A1 verdict and the Cost section.
2. Read the A1 rows (the cross row at P = 1 decides A1 per the lead). Then the Evidence table at 45f98eb and `campaign accept wheel-lib correctness
   --confirm F1=261112-045015-68d268` (the lead's).
2. A7 re-review by the Xapiand lead.

## Blocked on

Nothing.

## Evidence

At wheel:45f98eb. Review findings: F1=261112-045015-68d268 (suite with `shared`; fails without:
261112-045016-03086f, 261112-045020-d35b49, 261112-045024-2acb3a); A7 F6 = same suite (fails
without 261112-045028-3630df).

| Criterion | Evidence |
|---|---|
| A1 | pending: 261112-045107-d98a70 (D75 single), 261112-045109-600561 (cross); reported 261112-045112-1213da (default build). At 1cd7e7a: single 261111-234624-f5cc8e passed (1.049x, 0.924x), cross 261111-233422-3a6c0b P = 1 1.125x (A/A 35.1%) |
| A2 | 261112-045015-68d268 (suite); fails without: 261112-045042-c739f7 (no cascade), 261112-045044-6131c7 (no owner assertion), 261112-045047-01cc0b (next() unsaturated), 261112-045050-1426c6 (model fix), 261112-045016-03086f, 261112-045020-d35b49, 261112-045024-2acb3a (F1), 261112-045028-3630df (F6) |
| A3 | 261112-045015-68d268 (wheel_alloc in the suite) |
| A4 | 261112-045138-2d2840 (CI run 43221778598 green at 45f98eb: ASan+UBSan, TSan, matrix); local ASan+UBSan suite 261112-045105-3b0736 |
| A5 | 261112-045053-06a547 (fuzz_ops, 10 min, seeded with fuzz/corpus) |
| A6 | `docs/timer-wheel.md` (Cost (A1), Threading contracts), README Performance, at wheel:45f98eb |
| A7 | the Xapiand lead: CHANGES at f540679 (detent-wheel-lib-fit.md); F1, F3, F4, F6 answered at 45f98eb |

## Result

- `wheel.hh` at wheel:1cd7e7a; tests, model, fuzz target and corpus, bench, docs as listed in
  CHANGELOG. Cost section: docs/timer-wheel.md; README Performance.
- A1 under D75: the single row is met (1.049x at 1K, 0.924x at 1M). The cross row at 1cd7e7a
  misses at P = 1 (12.65 vs 11.24 ns, 1.125x) inside a 35.1% A/A band. 1cd7e7a has the same
  arm_remote, drain and expire as 94a51c6, whose cross row 261111-221548-4b10b6 passed (1.006x,
  1.035x, 0.937x). I read it as noise; it is a threshold miss, not a failure, since every run
  was valid. The rerun 261112-042845-919bf4 adds a sample. The lead rules on the criterion.
- Default build: clang keeps the harness's cand_w::arm out of line (inline cost 285 vs 225;
  cand_a::arm 205; 261111-233446-2a29ab): arm +0.67/+31 ns, re-arm +0.53/+16 ns at 1K/1M.
- Expire residue: +1.2 to +4.3 ns per entry at n = 1,000 (+3.98 in the D75 row); none at 1M.
  Cause untraced.

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
- Exp 5's comment on arm says a caller's wrapper inlines too; in the default -O2 build the
  harness's does not (cost 285 > 225). Left as is to keep evidence at 1cd7e7a; fold the wording
  ("a caller's call of arm inlines") into the review round's first code change.
- Review round: option 1 (owner-side drain of every stripe) over always fetch_or; reasoning
  under Doing. N2 as a uintptr_t compare, not std::less. F6 by bounding the destructor (one
  fire per entry, later arms undone) rather than a Debug assertion, so it is defined behaviour
  and testable in every build. The test hook is a macro, WHEEL_TEST_PUSHED, empty by default.
- Pushed the quest branch (only it) for CI, as the earlier rounds did; A4 cites CI at the commit.
- The default-build single row and the cross P = 1 row exited 1 on the 1.10x gate; every run was
  valid (no early, twice or missing fire): threshold data, not a failure, so nothing to fix.
