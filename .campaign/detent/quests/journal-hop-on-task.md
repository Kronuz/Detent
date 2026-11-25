# journal-hop-on-task: journal turn driver: fallback hop on a task primitive

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-hop-on-task`.

## Outcome

The turn driver's fallback `hop` (`turn_driver.h:412`) wants a task-side primitive (reviews/journal-driver-durability.md:125 F6, accepted as a task follow-up that no quest took); journal:904037a may already mitigate it, unverified. After this quest either journal:904037a is shown to close F6, or `hop` runs on a task primitive (a task-library change, filed in task first if one is needed).

## Acceptance criteria

- A1 [decision] Whether journal:904037a closes F6, with the evidence: a PLAN.md Decisions id.
- A2 If it does not, `hop` uses the task primitive and a test that fails on the old hop shows it (--fails-without): jobs.
- A3 The suite and TSan pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (R6).
