# consensus-simulation-asan-timeout: consensus_simulation stays well inside its timeout under ASan

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-simulation-asan-timeout`.

## Outcome

consensus_simulation under linux-asan-ubsan passes in 150-215 s, but hit its 300 s TIMEOUT
(CMakeLists.txt:60) in 4 of about 30 CI runs on 2026-11-22/23, among them main at consensus:df4f8f8
(run 45234099713), which passed in 171 s on another run of the same commit (45204437379); also
quest branches a3ee8ba twice (45158818381, 45204597820) and xapiand's 59ae07f. The distribution is
bimodal (tight at 170 s, then >300 s), which looks like a run that never finishes rather than a
slow runner. Find which: a seed or schedule that livelocks or hangs (a defect, fixed at its
cause) or a run time that depends on the host (then the test bounds its own work, not the timeout).

## Acceptance criteria

- A1 The cause is named with evidence: a local ASan run that reproduces a >300 s run (or a hang)
  with its seed, schedule and a stack or progress trace at the stall, or a measurement showing
  the time is host-dependent and by how much: jobs
- A2 The fix removes the cause; a test or `--fails-without` job shows it (a hang: the seed that
  hung passes in normal time; host dependence: the test's work is bounded so its ASan time stays
  under half its timeout on a 2x slower host): jobs
- A3 50 ASan runs of consensus_simulation at the fix, none over 200 s, as strict jobs of at most
  10 minutes each: jobs
- A4 The suite passes and `ci:linux-asan-ubsan`, `ci:ci` are green at the landed commit

## Contracts

None changed. Raising the TIMEOUT alone does not meet A2.

## Decisions

Lead, 2026-11-23: its own quest because it reddens main and every consensus quest's CI.

## May land early

None.

## Tasks

None.
