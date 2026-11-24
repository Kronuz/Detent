# consensus-liveness-asan-timeout: journal

State: waiting on jobs 261124-052615-4ff43a 261124-051832-d5a4f8, CI on consensus:01a109c
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

1. Read 4ff43a (ASan old vs new, same host, BOUND=90 on new) and d5a4f8 (custom mutants,
   sorted-line compare). If new ASan is not well under 90 s, cut harness cost further, keeping
   equiv.sh's hash equal.
2. CI times at 01a109c vs consensus_shared_worker (ci-times.sh); Result; Evidence table.

Results so far at e44fe39:
- 261124-051827-1d53c7: old == new == LIVENESS_VERIFY (trace hash per profile), 20 seeds.
- 261124-051829-5486f3, 261124-051830-677391: all 17 core mutants, same kills old vs new, same
  output lines (order differs only by line buffering). votes-and-pre-votes-without-the-log-check
  caught by both now.
- 261124-051846-6e6be3 (--fails-without, main): plain ASan 132.5 s, above 90 s.
- 261124-051825-444ce3 failed: phases.sh edited in place while it ran; replaced by 4ff43a.
- vote-ignores-leader-contact: caught by neither harness (gap, per the coordinator).
- Core finding for the lead: a leader's due D3 removal waits for a Tick that
  Core::next_deadline() does not report (LIVENESS_VERIFY found it), so a host sleeping until
  next_deadline() delays the removal.

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
