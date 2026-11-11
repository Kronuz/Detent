# coordinator-idle-flake: Flaky retained-idle check in test_delivery_coordinator under ASan

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status coordinator-idle-flake`.

## Outcome

`test_delivery_coordinator`'s retained-idle check (tests/detent_delivery/test_coordinator.cc:1117) passes every time. After the coordinator closes, the fixture server's `read_some` on the idle keep-alive connection reliably sees the close. It failed once under ASan+UBSan in CI run 43002602154 (attempt 1, Detent:06770a7e) and passed on rerun at the same commit. Whether the race is in the test or in the delivery client's close is to be found and fixed at its cause.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 The cause is named with a deterministic reproduction (a forced interleaving or a delay injection), and the reproduction fails before the fix: a job tagged --fails-without
- A2 The check passes 200 repeats under ASan+UBSan at the fix: one light job of at most 10 min

## Contracts

None.

## Decisions

D54, D55.

## May land early

Everything: tests and client close behavior behind tests.
