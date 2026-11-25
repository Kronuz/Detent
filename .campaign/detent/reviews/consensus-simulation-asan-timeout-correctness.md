# consensus-simulation-asan-timeout review: correctness

Commit: consensus:54c6599 (first reviewed at consensus:2db1a9d1cc1e)
Verdict: APPROVE
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1-A3 jobs as `campaign brief` lists them (bc5203 with main arm b61059; fab10d and the
  --fails-without ea8620; the nine A3 shards 102d1c..f34d60), measured at consensus:cbe0522. The
  only change since then is the docs commit consensus:2db1a9d. I did not rerun them.
- A4: CI 45299975989 attempt 2 at cbe0522. CI 45331244162 at 2db1a9d itself also ran:
  linux-asan-ubsan was green with consensus_simulation at 58.26 s. Its one failure is
  macos-x86_64 consensus_transport_groups_fetch-alongside-consensus "Subprocess aborted" after
  7.13 s, the flake that quest consensus-fetch-alongside-flake owns, outside this diff.
- Re-review at consensus:54c6599. `git diff 2db1a9d eb9153f` changes one bound: the heal loop's
  `round < 4000` becomes `round < 2000` at test/simulation_test.cc:240, with its comment and one
  sentence in docs/mechanisms/simulation.md. The merge 54c6599 adds only main's CMakeLists.txt and
  test/driver_install_request_test.cc. `git diff eb9153f 54c6599` is empty for the three quest
  files, and `git diff c177c88 54c6599` is exactly the quest's three files. The bound restores
  the old per-proposal budget: old proposed from round 2,000 with a deadline at round 4,000, and
  new proposes from round 0 with a deadline at round 2,000, so both give 2,000 rounds after the
  first proposal. It is tighter than old in one way: settling now counts against the same 2,000
  rounds, where old gave 2,000 settling rounds on top. The default seeds still fit. Their
  rounds before the stop match the 4,000-round run up to the majority, at most 713 rounds
  (median 130; job 261123-211702-747c2a), a 2.8x margin. Confirming jobs at the fix are
  261123-231348-ff18c4 and 261123-231353-be4231 (the lead's; I did not wait for them).

## What I checked myself

The scripts are under `.scratch/detent/review-consensus-simulation-asan-timeout-correctness/` in the
home. Each builds the fix's tree with asserts on at -O2 and compares the merge base's test
sources (consensus:3aa1355, "old") with the fix's ("new").

1. heal.sh, job 261123-211702-747c2a. It instruments the core heal (inst.py) and runs core
   seeds 1-200 x 10,000 steps plus the Driver seeds, at 60,000 ms against 1,000 ms (Q1, Q2).
2. mutants.sh, jobs 261123-220240-257404 (custom), 261123-221533-aacd58 and
   261123-222959-c8d242 (the 17 mutants of tools/mutants.sh), and 261123-223801-c6a00c and
   261123-225838-cefb04 (the 18 mutants of test/driver_mutants.py). Each mutant ran against the
   old and the new harness with CI's default arguments (Q1, Q2).
3. ci-times.sh reads consensus_simulation's linux-asan-ubsan time from 13 CI runs, next to
   consensus_liveness, consensus_consensus and consensus_shared_worker from the same job as
   runner-class controls. The output is in ci-times.txt (Q4).

Q1, the heal stop. No mutant separates old from new. Of 37 mutants, both harnesses catch the
same 12 and miss the same 25, which other suites own (unit, membership, ASan). Two custom
mutants, an unclamped follower commit and an append that ignores the previous index's term, also
give equal results: both harnesses miss the first and both catch the second. ack-before-turn-barrier
is caught by both. Old catches it as a heal-liveness failure at Driver seed 12; new catches it
earlier, as a fence at Driver seed 1. What the stop gives up is measured in finding F2: a little
catch-up state, never an asserted property. The fault phase is identical: same seeds, same RNG
stream, since heal() runs after it.

Q2, the 1,000 ms snapshot_timeout. The table below gives each seed's counters with the two
regression seeds (399, 11) subtracted; the last row is the default 20 seeds.

| Seed | SnapshotNeeded old → new | validated | activated | refused |
|---|---|---|---|---|
| 10 | 3 → 3 | 1 → 1 | 1 → 1 | 0 → 0 |
| 15 | 7 → 7 | 5 → 2 | 4 → 2 | 1 → 0 |
| 16 | 3 → 4 | 2 → 2 | 2 → 2 | 0 → 0 |
| 17 | 2 → 4 | 2 → 2 | 2 → 2 | 0 → 0 |
| 18 | 5 → 7 | 2 → 2 | 2 → 2 | 0 → 0 |
| 20 seeds | 45 → 77 | 33 → 34 | 30 → 32 | 3 → 2 |

Installs still complete in every seed, and timeout-driven restarts rise by 71% in aggregate.

Q4, the speed-up. It is real and it holds on every runner class seen. consensus_simulation's time
over consensus_liveness's, on the same runner:

| Code | Runs | consensus_simulation | Ratio to consensus_liveness |
|---|---|---|---|
| main | 8 passing | 171.45-278.78 s | 1.95-2.33 |
| main | 45234099713 | Timeout 300.17 s | above 1.78 |
| fix, typical class | b5e1d47, cbe0522 attempt 2, 2db1a9d | 57.38, 59.09, 58.26 s | 0.70-0.71 |

That is a 3.0x cut at a fixed runner class. On the slow class (cbe0522 attempt 1, where
consensus_shared_worker took 247 s against about 142 s) the fix took 112.52 s. On a comparable
slow runner main took 278.78 s (3aa1355, liveness 143 s), and it timed out on 45234099713.
Without ASan, a whole default run takes 6-29 s old and 3-10 s new, from the passing mutants'
times in the mutant jobs.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | The heal liveness oracle's budget changed, and the journal's "it now has to hold sooner" is not accurate. Old required a majority within 2,000 rounds of the first proposal (observed: median 13, max 264 rounds after round 2,000). New allows 4,000 rounds from the heal with proposals from round 0 (observed: median 130, max 713). A proposal-to-commit stall of 2,000-4,000 rounds failed old and passes new. A stall at the heal's start is now caught sooner. No mutant here exercises either case. | job 261123-211702-747c2a heal-{old,new}.txt; test/simulation_test.cc:240 | Fixed in consensus:eb9153f (D152): the loop is bounded at 2,000 rounds, the old per-proposal budget, with a 2.8x margin over the observed 713. Confirming jobs: 261123-231348-ff18c4, 261123-231353-be4231. |
| F2 | no | gap | Q3: "the same checks on less state" is real, but small and never asserted. Under old, after the majority applied, 102 lagging nodes in 109 of 200 seeds applied 155 more commands. That is 1.3% of the 12,041 applied at the heal's end, with 0 installs. It came with 1.71 M of the run's 6.16 M act() oracle passes, almost all heartbeats on a quiet network. New checks none of that. Neither version asserts that a minority converges: 270 nodes were still behind after old's 4,000 rounds. | job 261123-211702-747c2a (HEAL lines); mutant jobs above | None needed for this quest. Asserting that every up voter applies "healed" would be a new oracle outside this quest. |
| F3 | no | nit | The `fail(`/`expect(` counts cannot show that no oracle changed, since a check can stay in the source and run on less state (F2). They do not match my count either: git grep finds 22 `fail(` and 62 `expect(` lines in simulation_test.cc and 31 `fail(` lines in driver_simulation.cc at both 3aa1355 and 2db1a9d, not "82 / 31". The proof is the diff, which touches no check, plus the heal and mutant probes. | `git -C consensus grep -c -F 'fail(' 3aa1355 2db1a9d -- test/` | Cite the diff and the probes in the journal instead of the counts. |
| F4 | no | nit | Seed 15 loses its one image refusal and two activations at 1,000 ms. The refusal path is still reached twice in the default 20 seeds. | heal job, drv-{old,new}-15.txt | None. |
| F5 | no | nit | docs/mechanisms/simulation.md narrates change in places ("instead of idling 2,000 rounds", "(171 s before)"), not present-tense mechanism. | docs/mechanisms/simulation.md:38,46 | Optional docs-cleanup pass. |

## Not run

- No mutant ran under ASan, so the two resume-before-unlink mutants that need ASan were compared
  in Release only. Both harnesses miss them there, as expected.
- Seeds other than the default 1-200 core and 1-20 Driver seeds (CI's) were not compared old
  against new.
- A3's 50 ASan runs and A1/A2 were not rerun; I accepted the contributor's jobs.
- I did not merge main's newer CMakeLists.txt change; that happens at land.
