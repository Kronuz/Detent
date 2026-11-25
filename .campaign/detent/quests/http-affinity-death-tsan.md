# http-affinity-death-tsan: http: fix coroutine_test's affinity death test race under TSan

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status http-affinity-death-tsan`.

## Outcome

coroutine_test's affinity death test fails 5 of 10 runs under TSan at http:eb08b59 (http-idle-cost journal:155-160; reviews/http-idle-cost-correctness.md:58 C3), reported for the lead and unfixed through http:ec273dd. After this quest the test passes under TSan reliably, or the race is shown to be in http and fixed there.

## Acceptance criteria

- A1 Rerun first: the failure rate at http main under TSan (20 runs): jobs.
- A2 The death test passes 20 of 20 under TSan after the fix: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J6).
