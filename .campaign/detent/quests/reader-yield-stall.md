# reader-yield-stall: Consensus transport: a reader's yield resumes promptly under load

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-yield-stall`.

## Outcome

Under load a connection's reader parks at connection.h's `co_await task::yield()` after a successful `SSL_read` and is not resumed for 0.4-1.2 s while its loop runs other handlers (transport-feed-scan's diag 261117-204455-6f15b3: 4 of 12 runs stuck; 2-100 KB unread in the socket). Senders then hold credit and memory, and connection_test's coarse_writer reads 48 frames in 6.5 s against a 2 s dead_after. After this quest a yielding reader resumes within a bound tied to the loop's ready work, not to how long other handlers run, and a reader never starves behind its own loop's posts.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 The cause is named: which queue the yield's continuation goes to and what runs ahead of it, shown by a trace job.
- A2 A test forces the stall (a loop kept busy with other handlers while a reader yields) and fails at main: the reader's resume latency exceeds a stated bound; it passes at the fix x20 at 4-way load: jobs.
- A3 Fairness holds: no group or connection is starved by the fix, and TSan passes the transport tests: jobs.
- A4 transport-feed-scan's A2 idle-cost run no longer needs its `uncredited` wait to settle, or the doc states why it still does: a job.
- A5 The suite passes, plus CI.

## Contracts

No API change expected; if the fix is in reactor or task (not consensus), stop and report it to the lead.

## Decisions

Filed by the lead 2026-11-17 from transport-feed-scan's Finding (its journal, session 2). Likely the same cause as connection-slow-reader-flake's coarse_writer failures.

## May land early

None.
