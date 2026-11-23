# docs-site-architecture: Docs refresh: architecture

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status docs-site-architecture`.

## Outcome

The architecture pages (architecture.md, architecture/**, about, benchmarks) describe Detent as it runs on `main` at the landed commit, rewritten as a whole from `docs/dev/docs-site-audit.md`'s rewrite scope for this area (D52). Every command, request and code sample on them runs in the docs E2E with an assertion block, and every feature the audit lists as undocumented for this area has a section with runnable examples.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 [artifact] Every audit row for this area marked stale, wrong, missing content or delete is fixed or deleted, each fixed claim citing its source: the rows checked off in the journal's table
- A2 Every runnable block on the area's pages has an assertion block, and the area's entries are gone from `tests/docs/e2e_exceptions.txt`: the `e2e` check
- A3 Every feature the audit lists as undocumented for this area has a page section with runnable examples and assertions: the `e2e` check, and the journal names each section
- A4 `check_docs.py` passes with no exception left for the area's pages: the `docs` check
- A5 The site builds with no broken internal link from or to the area's pages: the `site` check
- A6 [manual] The owner reads the area's rendered pages before landing: the lead publishes them (an artifact or the built site) and records his verdict

## Contracts

Consumes `docs-e2e`. Changes only this area's pages; a fix another area needs goes to that quest by message.

## Decisions

D43, D44, D46, D52. The owner reads each area before it lands (D52).

## May land early

None: the owner reads the whole area first.
