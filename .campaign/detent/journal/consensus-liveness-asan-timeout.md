# consensus-liveness-asan-timeout: journal

State: working
Quest: .campaign/detent/quests/consensus-liveness-asan-timeout.md (the lead's; never edit it)

## Doing

Fresh contributor (2026-11-24) resuming after the first stalled uncommitted. Its edit is
committed as consensus:f63a75c (idle windows scaled by election_max; default profile unchanged,
since window(default, ms) == ms). Probes live in the home's
`.scratch/detent/consensus-liveness-asan-timeout/` (common.sh, phases.sh, mutants.sh,
instrument.py); smoke-testing mutants.sh baseline before submitting jobs.

## Next

1. Submit A1 phases job at main (512e3ef), A2 phases job at the fix, A3 mutant shards.
2. Write docs/mechanisms/liveness.md (windows, costs).
3. Push; read CI times (liveness vs consensus_shared_worker, same runner).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |

## Result

Pending.

## Rulings I made

- R1: idle windows (heal isolation, lone isolation and settle, witness settle, removed-voter
  return) scale with election_max; deadlines (commit_one's 20 s/30 s, transfer's 10 s) and every
  asserted bound stay in absolute ms, unchanged. The default profile's run is unchanged.
