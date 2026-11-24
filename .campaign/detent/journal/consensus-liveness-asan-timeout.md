# consensus-liveness-asan-timeout: journal

State: waiting on jobs 261124-040934-6f74ca 261124-040939-f3dbf8 261124-040943-062bfb 261124-040947-6ef39b 261124-040951-86f5e6, CI 45390900739
Quest: .campaign/detent/quests/consensus-liveness-asan-timeout.md (the lead's; never edit it)

## Doing

Fix is consensus:1bbef40 (test) + 0894152 (docs/mechanisms/liveness.md). Old contributor's edit
committed as f63a75c; 1bbef40 drops its inaccurate "hundred times" cost claim. Probes in the
home's `.scratch/detent/consensus-liveness-asan-timeout/` (common.sh, phases.sh, mutants.sh,
instrument.py, ci-times.sh). Old build dirs in the worktree deleted.

Jobs out:
- 261124-040934-6f74ca A2 --fails-without at main: MAX_RATIO=3, a profile above 3x the default's events.
- 261124-040939-f3dbf8 A2 at fix: plain ASan old vs new (same host), new per-phase, MAX_RATIO=3.
- 261124-040943-062bfb, 261124-040947-6ef39b A3: tools/mutants.sh core mutants, old vs new harness.
- 261124-040951-86f5e6 A3: 12 custom mutants (no pre-vote, pre-vote raises own term, pre-vote
  ignores leader contact, pre-vote counts refusals, vote ignores leader contact, lone campaigns
  after 15000 ms / after 12 election timeouts, witness never / late (3 timeouts, 1500 ms) hand-off,
  hand-off target ignores liveness, no CheckQuorum).
- CI 45390900739 at 1bbef40 (pushed): liveness vs consensus_shared_worker ratio.

## Next

1. Read the jobs; the custom mutant lone-campaigns-after-12-election-timeouts is expected to
   separate old (FreeBSD's lone window was 104 timeouts) from new (10 everywhere, the default's);
   report it either way.
2. CI times at fix vs main (ci-times.sh), docs table, Result, push 0894152.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | job 261124-040930-6e9c88 (main 512e3ef, ASan, 20 seeds): plain 224.6 s local; per phase, derived profiles 184.8 of 192.3 s; lone 87.4 s, removed 43.4, witness 36.2, heal 19.9; FreeBSD 14.16x the default's events (arm64 7.50x, Linux 5.13x) |

## Result

Pending.

## Rulings I made

- R1: idle windows (heal isolation, lone isolation and settle, witness settle, removed-voter
  return) scale with election_max; deadlines (commit_one's 20 s/30 s, transfer's 10 s) and every
  asserted bound stay in absolute ms, unchanged. The default profile's run is unchanged.

## CI times (linux-asan-ubsan), consensus_liveness / consensus_shared_worker

- main 512e3ef (45366654906): 173.49 s / 238.35 s = 0.728 (slow class; 3.6% under 180 s).
- main 54c6599 (45357692305): 82.09 / 139.12 = 0.590 (typical class).
- Review table (.campaign/detent/reviews/consensus-simulation-asan-timeout-correctness.md): 0.54-0.73 over 13 runs.
