# consensus-liveness-asan-timeout: journal

State: ready to land
Quest: .campaign/detent/quests/consensus-liveness-asan-timeout.md (the lead's; never edit it)

## Doing

Round at consensus:e0fd383 (main merged with git merge, 50 commits; clean; core.h changed on main).
Local check at the merged head, release -O2: consensus_liveness_test 20 seeds 1.7 s, 0 failures;
LIVENESS_VERIFY 3.9 s, 0 failures.

A1's old-harness job 261124-072649-6d7137 failed in its harness (alarm 14 at 400 s on the old
instrumented run, then a parser StopIteration). Harness fixed in phases.sh (not committed: .scratch
is the home's): a baseline (old) run killed by the alarm now prints "over the alarm (400 s)" and
is a result (rc 0, no per-phase table); the new harness over the alarm still fails the job; BOUND=90
on the new plain run unchanged (D152); the parser tolerates an empty table. Signal death shows as
-14 in Python and 142 in bash; both are mapped.

Evidence at e0fd383, all queued once: 261124-113944-9df262 (A2, exclusive same-host A/B, BOUND 90),
A3 261124-114001-fe77b1 (trace hash and VERIFY), 114006-501c4f / 114010-09df4a (core mutants),
114015-8765f9 (custom mutants), A1 261124-123229-68cd34 (old per-phase, committed probes; 114019-068816 failed in its parser). The A3 jobs are
submitted with --inputs on include/consensus/**, src/** and the test: `campaign job rerun` reused
the c4c70f8 results, since a job's identity is only the files its command names, and main changed
core.h. CI on the pushed head: see the check (the macos-x86_64 consensus_transport_rpc_host_fetch
stall is consensus-fetch-oversize-abort's, outside my files, not touched).

## Msg from the coordinator (A1 068816 failed again)

Cause: phases.sh's parser matched "steps", but instrument.py prints "events", so the table was
empty and max() raised; the old run had finished (no alarm). Not the uncommitted edit: the job ran
that file. Fix: the regex takes events|steps; an empty table exits "no per-phase rows"; a baseline
over the alarm prints "over the alarm (400 s)" (rc 0). Tested on the real 068816 log (EVENTS rows,
rc 0) and an empty log (message, rc 1). The probes are committed in the home's tree at
Detent-wt .campaign/detent/probes/consensus-liveness-asan-timeout/ (537e942b), and A1 is requeued once
from there: 261124-123229-68cd34 at e0fd383. A2 9df262 passed with BOUND=90, the fix's own total.

## Next

Ready to land. The probes are on home main (.campaign/detent/probes/consensus-liveness-asan-timeout); home main merged into this journal branch (diff to main is the journal only).

## Next (done)

1. When the jobs and CI finish: paste the brief's table into Evidence; State ready to land if
   linux-asan-ubsan passes and only the known macos-x86_64 fetch stall (other quest) fails.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261124-123229-68cd34 (passed at e0fd383: old instrumented run over the alarm at 400 s, recorded as a result; supersedes 261124-114019-068816, which failed on the harness regex bug and printed the table: FreeBSD 13.45x, Linux 5.04x, arm64 7.38x the default events); table: 261124-061759-e60de0 (main's harness at e71996d, ASan per phase: derived profiles 96% of the time; FreeBSD 14.2x the default's events; main's test file is unchanged) |
| A2 | 261124-113944-9df262 (exclusive A/B at e0fd383); earlier 261124-072638-7efa59 (c4c70f8: ASan 129.7 -> 64.8 s); CI linux-asan-ubsan 40.79 s (0.284 of consensus_shared_worker; main 0.590-0.728); --fails-without at main 261124-051846-6e6be3 (plain ASan 132.5 s > 90 s) |
| A3 | 261124-114001-fe77b1, 261124-114006-501c4f, 261124-114010-09df4a, 261124-114015-8765f9 (at e0fd383; earlier at c4c70f8: 261124-072640-a63185, 072642-7d4f31, 072644-b5fbdb, 072646-006a62) |
| A4 | `ci:ci` (run 45472511590 at e0fd383, green on every job) |

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
