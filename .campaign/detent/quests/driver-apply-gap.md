# driver-apply-gap: consensus: a Driver voter's apply index passes entries its application never received

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status driver-apply-gap`.

## Outcome

The Driver simulation fails at seed 399, 3,000 steps (`SIM_DRIVER_SEEDS=1 consensus_simulation_test 1 3000 399`), on consensus main 5923b6c and on consensus-close-fenced's a699c96 (job 261118-124515-cf5a72; Xapiand's T2 budget run 261118-115900-e84a48 found it, over journal 4272229, 4d364f1 and d90797c alike; all 10,000 Core seeds pass): "the healed group did not apply a new entry on every data voter". Nodes 1 and 2: committed 120, applied 120, application 120; node 3: committed 120, applied 120, application 0, held 0, in a run of 8 elections, 8 crashes, 306 proposals, 8 transfers and 31 cancels. A voter's apply index advanced past entries its application never received. After this quest the path is named (restart, transfer, install or cancel) and fixed so that a voter's apply index never passes what its application received; seed 399 is a regression test.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 [manual] The path that advances apply without delivering to the application is named from a trace of seed 399, with the step and event sequence in the journal.
- A2 A deterministic test (driver_apply_test or a driver_test case) reproduces that path and fails without the fix; seed 399 passes with it: jobs.
- A3 The Driver simulation passes 10,000 seeds x 3,000 steps (split into light jobs chained per D120), and the A2 test under TSan and ASan: jobs.
- A4 DRIVER.md states the invariant (apply index never passes the application's delivered index) where it describes apply: [artifact] `docs/DRIVER.md`.
- A5 The suite passes, plus CI: jobs.

## Contracts

None: the Driver's API is unchanged.

## Decisions

Filed 2026-11-18 from Xapiand's T2 run; high risk (correctness of applied state). Needs consensus-close-fenced (driver.h); consensus-worker-release and consensus-faultfs order after it.

## May land early

The regression test alone, disabled, if it helps bisect: no.
