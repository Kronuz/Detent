# okt-followups: One knob type: review follow-ups C1-C5

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status okt-followups`.

## Outcome

The non-blocking findings of `reviews/one-knob-type-correctness.md` (C1-C5) are closed: tests that fail without the behavior they claim, and two small cleanups.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 C1: the non-canonical and past-64-bit varint cases sit in the paged body (not the header) and fail when the body decoder accepts them: suite plus a `--fails-without` mutant.
- A2 C2: a flat-image record with default != 0 under a percentage evaluates in and out of the bucket (D68), reading the rollout value at its offset: suite plus a mutant.
- A3 C3: the Python and Rust binding tests check `get_bool` refuses 2 and -1: suite.
- A4 C4, C5: `LeafBody` refuses a value without any percentage; the export encoder's `flag_` is `definition_`: suite.

## Contracts

None.

## Decisions

D65, D68; the review's findings table.

## May land early

Everything (tests and internal cleanups).
