# transport-idle-budget: Consensus: consensus_transport_idle fails its budget at 4 loops

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-idle-budget`.

## Outcome

`consensus_transport_idle` passed in transport-feed-scan's suite 261118-051311-f0eff6 at consensus:5923b6c (05:13Z) and fails 3 of 3 at the same commit in 261118-072938-1fb596 (07:29Z): "FAIL: an idle connection end within 48 KB, by the fit (loops 4)", per host 108-109% of 52 KB + 2P x 44 KB at 4 loops, 100% at 1. Same code, so something outside the commit moved: the job's environment (OpenSSL 4.1.0, build slot, allocator state, a cached dependency) or the test's own sensitivity to it. It blocks every consensus quest's suite gate. After this quest the test passes, and the journal names what moved with a job that shows it.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 [manual] The cause is named: what differs between f0eff6 and 1fb596 (job specs, environment, slot, dependency versions, the test's log lines), shown by a job that reproduces both outcomes on demand.
- A2 The fix (the test's fit if loops add real per-host cost, or the environment if a dependency leaked in) keeps the budget meaningful: a mutant that adds 4 KB per connection end still fails it: jobs.
- A3 The suite passes, plus CI.

## Contracts

None.

## Decisions

Filed 2026-11-18 from consensus-close-fenced's suite; pre-existing on main.

## May land early

None.
