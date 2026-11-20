# journal-hold-loop-required: journal Hold takes its loop as required and refuses an empty one (D131)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-hold-loop-required`.

## Outcome

journal's `Hold` cannot be built without a loop: the loop parameter is required, and an empty `scheduler_ref` throws `std::logic_error` at construction, as `durable()` refuses a scheduler without `post_call` (D131). A deaf blocking entry, the defect consensus-blocking-hold-loop and journal-blocking-wake fixed, can no longer be written.

## Acceptance criteria

- A1 `Hold` without a loop does not compile, and `Hold` with an empty `scheduler_ref` throws `std::logic_error` before it holds anything; a test shows the throw and fails without the change (--fails-without): jobs.
- A2 journal's suite and TSan pass, and consensus and Detent build against it unchanged: jobs.
- A3 [artifact] The CHANGELOG and docs/mechanisms/turn-seal.md state the rule: `CHANGELOG.md`.

## Contracts

Changes journal's public `Hold` constructor (no default loop). Every known caller already passes one.

## Decisions

D131 (the lead).

## May land early

None.
