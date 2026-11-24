# consensus-liveness-asan-timeout: journal

State: waiting on jobs 261124-061759-e60de0 261124-061800-6a30e3 261124-061802-f328be 261124-061803-1460cf 261124-061805-bab941 261124-062325-f25d84, CI 45410562603 (8511ff1)
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

1. Coordinator (latest): A1/A2 bar is CI's ASan run <= 90 s; profile where the rest goes (lone
   50%, removed 20%) and cut with the same exactness proof. consensus:e71996d (typed queue events,
   cut bits) is at 0.50 of main on the same host: 129.7 -> 64.8 s (261124-061757-8fb9aa, which
   failed only in phases.sh's ratio code on a zero event count; fixed: instrument.py now counts
   core steps). Profile f25d84 pending; push e71996d after CI 45410562603's ASan time is read.
2. Then: Evidence table, Result, docs cost table, ready for review.

Since the last entry:
- consensus:3d647b6 keeps each node's single Tick on the node (time, order) beside the queue;
  consensus:8511ff1 reads the hold deadline lazily at the Tick. Both trace-checked locally
  (old == new == verify, release).
- ASan A/B same host, 20 seeds (strict): e44fe39 157.6 -> 105.4 s (261124-052615-4ff43a; failed
  only its BOUND=90); 3d647b6 159.7 -> 84.3 s, 0.53 of main (261124-054710-31c4e2).
- Custom mutants at e44fe39 (261124-051832-d5a4f8): all 12 same kills, same output lines; incl.
  lone-campaigns-after-12-election-timeouts and -15000ms in the same profiles as main.
- Profile at 3d647b6 (261124-054712-948bdb): Core::step 49% (real deliveries and their Ticks),
  next_deadline 7%, queue 2.5%. What is left is core work on events main also delivers.
- CI 45390900739 at 1bbef40 (scaled windows, withdrawn): liveness 16.80 s.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | job 261124-040930-6e9c88 (main 512e3ef, ASan, 20 seeds): plain 224.6 s local; per phase, derived profiles 184.8 of 192.3 s; lone 87.4 s, removed 43.4, witness 36.2, heal 19.9; FreeBSD 14.16x the default's events (arm64 7.50x, Linux 5.13x) |

## Result

Pending.

## Rulings I made

- R2 (supersedes R1, withdrawn after msg from the coordinator: scaled windows lost kills): the
  windows keep main's milliseconds; cost is cut by holding early Ticks, exact by construction
  and checked by trace hash and LIVENESS_VERIFY.
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
