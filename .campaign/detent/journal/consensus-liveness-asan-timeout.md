# consensus-liveness-asan-timeout: journal

State: waiting on jobs 261124-072638-7efa59 261124-072640-a63185 261124-072642-7d4f31 261124-072644-b5fbdb 261124-072646-006a62 261124-072649-6d7137, CI 45425539843 (c4c70f8)
Quest: .campaign/detent/quests/consensus-liveness-asan-timeout.md (the lead's; never edit it)

## Doing

Coordinator's message (scaled windows lose kills, D152): window scaling abandoned. A3 jobs
261124-040943-062bfb and 261124-040951-86f5e6 showed main's harness alone catching
votes-and-pre-votes-without-the-log-check ("a node fenced", Linux seed 7 heal: probe in
.scratch where/), lone-campaigns-after-12-election-timeouts (arm64, Linux, FreeBSD), and
lone-campaigns-after-15000ms in 4 profiles vs 1.

New fix (consensus:3d788fe, f2fb... see git log): main's windows unchanged; the harness holds a
node's Tick before Core::next_deadline() (read after each core step) instead of stepping it, still
drawing its delay, and delivers it before the node's next event. A leader's Tick is held only with a
settled membership and not as a witness (a D3 removal waits for a Tick: next_deadline() does not
report it; found by LIVENESS_VERIFY). Messages move instead of copy. Exactness: trace.py hashes
every action list (time, node, kind, message kind+term); old == new == LIVENESS_VERIFY in every
profile (job 261124-050515-3d9f0f at 3d788fe). Release 20 seeds: 5.1 s -> 2.0 s locally.

Profiles: old harness 261124-044728-e459f6; new (followers only) 261124-051424-4f6936: Core::step
53%, leader drive 36% (replicas() set per tick 14%), next_deadline 10%, queue 12%.

Jobs out at e44fe39: 444ce3 (A2 ASan old vs new same host), 1d53c7 (equiv), 5486f3/677391/d5a4f8
(A3 mutants, now also reporting identical output), 6e6be3 (A2 --fails-without at main: plain ASan
above 90 s). 261124-040934-6f74ca is invalid (hit its own 280 s alarm, rc 142): superseded by 6e6be3.

## Next

Code frozen at consensus:c4c70f8 (coordinator's standing rule: one evidence set per round; no
push while it runs). Docs commit b8577c1 is local, unpushed.
1. When the set finishes: read 7efa59 (exclusive same-host A/B, wall and CPU) and CI
   45425539843's ASan time; merge main once (git merge, never rebase); push; paste the brief's
   table; ready for review.

Since the last entry:
- CI 45415901609 at e71996d (serial typed events) ran on a slow runner: consensus_liveness
  97.61 s, over 90 s (shared_worker 241.58 s, ratio 0.404; consensus_consensus 6.95 s, so liveness
  is 14.0x it, as at 8511ff1; main 28.6-38.6x).
- consensus:c4c70f8 runs the four profiles on threads, each into its own ordered report, printed
  in profile order; output and the 20-failure cap equal the serial run's (local trace check:
  old == new == verify). CPU unchanged; wall is bounded by FreeBSD (~43%). TSan: Core headers
  hold no mutable statics; report is thread_local.
- Probes are thread-safe now (trace.py thread_local hash via say(); instrument.py thread_local
  profile, mutex on its maps); phases.sh prints wall and CPU.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261124-061759-e60de0 (main's harness at e71996d, ASan per phase: derived profiles 96% of the time; lone, removed, witness, heal; FreeBSD 14.2x the default's events); also 261124-040930-6e9c88 at main |
| A2 | pending 261124-070728-3cfde4; CI 45410562603 (8511ff1) consensus_liveness 40.79 s vs consensus_shared_worker 143.55 s = 0.284, main 0.590 (54c6599, typical) and 0.728 (512e3ef, slow, 173.49 s); --fails-without at main 261124-051846-6e6be3 (plain ASan 132.5 s > 90 s) |
| A3 | 261124-061800-6a30e3 (trace hash old == new == LIVENESS_VERIFY, every profile), 261124-061802-f328be, 261124-061803-1460cf (17 core mutants), 261124-061805-bab941 (12 custom): same kills, same output lines |
| A4 | pending: CI at the head |

Jobs examined and set aside: 261124-040934-6f74ca (hit its own alarm), 261124-040939-f3dbf8 and
261124-040943-062bfb/-6ef39b/040951-86f5e6 (scaled windows, withdrawn: they showed the lost
kills), 261124-050513-9f2460, 261124-051825-444ce3 (script edited under it), 261124-052615-4ff43a
(BOUND), 261124-054710-31c4e2, 261124-055736-3b5693 (cancelled), 261124-063621-626832 (load).

## Result

consensus_liveness keeps main's test exactly and runs at half the cost or less. Windows,
seeds, profiles and every check are main's (D152). The harness (test/liveness_test.cc) holds a
node's Tick that falls before Core::next_deadline() instead of stepping it, still drawing its delay,
and delivers it before the node's next event; a leader holds only with a settled membership and
not as a witness (R3). Each node's Tick sits beside the queue; queue events are typed records;
messages move. Proof of exactness: per-profile trajectory hash and LIVENESS_VERIFY equal to main's
harness; 29 mutants give identical kills and output lines, including the three the scaled
windows lost. Cost: ASan same host 129.7 -> 64.8 s; CI linux-asan-ubsan 40.79 s (0.284 of
consensus_shared_worker, main 0.590-0.728). Docs: docs/mechanisms/liveness.md. Open: Core cost
per step (consensus-next-deadline-removal and any Core work are other quests);
vote-ignores-leader-contact is caught by neither harness (gap).

## Rulings I made

- R2 (supersedes R1, withdrawn after msg from the coordinator: scaled windows lost kills): the
  windows keep main's milliseconds; cost is cut by holding early Ticks, exact by construction
  and checked by trace hash and LIVENESS_VERIFY.
- R4: profiles run on threads. It cuts wall time, not CPU. Splitting consensus_liveness into
  one ctest per profile would do the same through CMakeLists.txt, which is outside this quest's
  file set.
- R3, the D3 removal omission (filed as consensus-next-deadline-removal; Core untouched here):
  Core::removal() is due only when committed_membership().leaving != 0 and
  committed_membership() == membership() (core.h:286-288). holdable() never holds a leader
  whose membership() has `leaving` set or differs from its committed membership, nor a witness
  leader, so a leader with a removal due always has its Tick stepped. When
  consensus-next-deadline-removal lands, holdable() can drop to `votes_in(...)` and the
  verify run shows whether it is exact.

- R1: idle windows (heal isolation, lone isolation and settle, witness settle, removed-voter
  return) scale with election_max; deadlines (commit_one's 20 s/30 s, transfer's 10 s) and every
  asserted bound stay in absolute ms, unchanged. The default profile's run is unchanged.

## CI times (linux-asan-ubsan), consensus_liveness / consensus_shared_worker

- main 512e3ef (45366654906): 173.49 s / 238.35 s = 0.728 (slow class; 3.6% under 180 s).
- main 54c6599 (45357692305): 82.09 / 139.12 = 0.590 (typical class).
- Review table (.campaign/detent/reviews/consensus-simulation-asan-timeout-correctness.md): 0.54-0.73 over 13 runs.
