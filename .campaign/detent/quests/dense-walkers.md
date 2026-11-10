# dense-walkers: One bounded inline-slot walker for Dense pages

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status dense-walkers`.

## Outcome

Every in-page walker that follows inline slots (about 11 today: paged_read_state.cc, paged_cursor.cc, paged_trie_overlay.cc and the key-driven walkers) advances through one shared bounded step that enforces slot progress and the page end, so the COR-1 class (one copy missing a bound) cannot recur. Bytes on disk, digests and read results are unchanged. From review dense-pages-design DES-6 (D42).

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 `git grep decode_node( src` shows decode_node called only from the shared step: a job
- A2 Native, the Dense fuzz smoke under ASan/UBSan and the 5M model oracle pass unchanged: the `native` gate and jobs
- A3 A mutant dropping the shared step's progress bound is killed by a test: a job
- A4 Read-path cost does not regress: an exclusive A/B against main with an A/A spread, honest magnitude

## Contracts

Internal to detent_local; no format or API change.

## Decisions

D42 (lead).

## May land early

Tests that pin current walker behavior.
