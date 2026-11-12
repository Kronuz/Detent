# superseded-sweep: List superseded cpp-libraries, successors and users

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status superseded-sweep`.

## Outcome

`docs/dev/superseded-libraries.md` lists every unarchived Kronuz repository that is a C++ library
(the cpp-libraries family; `gh repo list Kronuz`), and for each one that another library or a
design decision supersedes: its successor, the evidence (a PLAN decision, a README line, a
measured result), every current user (repository and file, from each repo's `CMakeLists.txt`,
`cmake/dependencies.json` and `#include`s across `~/code/Kronuz/*`, main checkouts only), and the
migration each user still needs. stash, scheduler and cluster are rows; the lead archives each
row whose users list is empty (D73).

## Acceptance criteria

- A1 [artifact] The ledger has one row per superseded library with successor, evidence, users and
  migration, and names the libraries checked and judged current: `docs/dev/superseded-libraries.md`.
- A2 [manual] The lead spot-checks three rows' users with `grep` against the repos.

## Contracts

None.

## Decisions

D72, D73. Judging a library superseded without a decision or a README statement saying so is a
proposal: list it under "Proposed" with the reason; the owner decides.

## May land early

None.
