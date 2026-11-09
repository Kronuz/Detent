# Efficiency ledger: the campaign tool against the earlier process

The owner asked (2026-11-08) whether the `campaign` workflow is more efficient than the earlier
ledger-and-agents process. Each row is one quest; numbers come from agent completion records
(tokens, tool uses, duration), `campaign usage`, job records and git, never estimates.

## Measures

1. Agent tokens per landed quest, contributors and reviewers separately.
2. Wall-clock from `campaign start` to `campaign land`.
3. Rework: review rounds, fix rounds, reruns caused by stale evidence.
4. Lead tokens spent coordinating (reports, relays, rereads), estimated from the lead's turns.
5. Defects that reached `main` and were found later.

## Baseline: the earlier process (2026-11-03 to 11-07)

| Work | Agent tokens | Duration | Outcome |
| --- | ---: | ---: | --- |
| S1/S2 developer (to checkpoint) | 480,813 | 1 h 35 m | S2 written, evidence owed; then 3 days of run-queue retries (expired TLS fixture, harness caps) before the timings existed |
| Reader S5+S6 developer | 340,924 | 8 h 6 m | Built; CI never green; security review blockers after |
| Legacy wave 2 F-H developer | 713,285 | 2 h 25 m | Built; CI red on S6's race |
| Dense-page designer | 96,411 (final leg) | 2 h 29 m | Design ready for review |
| S3b architect review | 244,709 | 59 m | APPROVE WITH CONDITIONS |
| Landed in that window | | | 0 of those 5 workstreams |

## This campaign (from 2026-11-08)

| Quest | Role | Model | Agent tokens | Duration | Rework | Landed |
| --- | --- | --- | ---: | ---: | --- | --- |
| namespace-s1-s2 | contributor | sonnet high | 147,377 (leg 1) | 20 m | 1 fix round (D1, D2) | |
| namespace-s1-s2 | durability review | opus high | 204,246 | 31 m | found D1, a regression against main that every earlier run missed; D2, a harness that could never produce evidence | |
| dense-pages | contributor | opus high | 255,439 (leg 1) | 32 m | A10 diagnosed (two harness defects, 3 days stalled before) | |
| dense-pages | contributor | opus high | 208,686 (fresh resume) | 18 m | 5M abort was a production defect (codec default limits refused stores over 504,096 keys); fixed at the type level | |
| dense-distribution | design | opus high | 298,559 (leg 1) | 33 m | | |

## Observations

1. Job identity covers the whole tree minus `.campaign/`, so a docs commit makes every earlier job stale: write docs last (S1/S2, 2026-11-08).
2. The first review on the campaign found what the earlier process did not: S1/S2's D1 is a crash window that 222/222 ctest, ASan, TSan and the old mutants all passed over, because T7 never cut store creation.
3. Evidence that cannot fail is caught: D2 showed the new mutant jobs exit 125 (no test target built), which a "must fail" job cannot tell from a kill; the harness now has to separate the two.
4. Three contributors plus a reviewer share one machine: their jobs queue behind each other's exclusive holds, so wall-clock is bound by the host, not the agents.
5. Queue wait dominates: by 2026-11-09 the campaign's 293 jobs ran 7 h 05 m and waited 100 h 17 m (`campaign usage`), mostly behind other campaigns' light holds. `campaign usage` counts cached input (95% of 154.0M tokens); the token columns above come from agent completion records and are not comparable to it.
