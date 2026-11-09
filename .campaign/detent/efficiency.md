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
| namespace-s1-s2 | contributor | sonnet high | | | | |
| dense-pages | contributor | opus high | | | | |
| dense-distribution | design | opus high | | | | |
