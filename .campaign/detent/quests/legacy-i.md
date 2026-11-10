# legacy-i: Legacy I: one local image format; flat-v1, image_format and legacy_anchor removed

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status legacy-i`.

## Outcome

The local store has one image format, Dense v3 (D43, D35 O18). The `flat-v1` image, the `image_format` configuration option, `legacy_anchor` and every name that only distinguishes formats (`paged-v2` included) are gone from source, tests and docs; a store in any other format takes the single refuse-and-refetch path.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 `git grep -nE 'flat-v1|flat_v1|image_format|legacy_anchor|paged-v2'` over src, include, tests and docs finds nothing: a job
- A2 A configuration naming `image_format` is refused with an error naming the unknown key: the `native` gate
- A3 The full native suite, and the Dense fuzz smoke under ASan/UBSan, pass: the `native` gate and a job

## Contracts

Removes the `image_format` configuration key (owner-approved, D35 O18).

## Decisions

D35 O18 (owner), D43 (owner). Baggage found by the dense-pages D43 round.

## May land early

None.
