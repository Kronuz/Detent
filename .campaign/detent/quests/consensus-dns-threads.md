# consensus-dns-threads: Consensus: DNS lookups run on bounded, owned threads

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-dns-threads`.

## Outcome

Xapiand's goal5-audit G5-C1 (consensus:42cb3cf, docs/GOAL5-AUDIT.md): `Lookup::start` (resolve.h:99) runs each DNS lookup on a detached, unowned thread and `abandon()` only stops delivery, so across transport rebuilds the thread count is bounded only by the resolver's timeout. After this quest lookups run on threads the transport owns, with a fixed bound on how many run at once; abandon cancels or reclaims, and shutdown joins them all.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 A test with a resolver that blocks (never answers) rebuilds the transport 1,000 times and the process's thread count stays at or under the bound: fails at main: a job.
- A2 Shutdown and destruction join every lookup thread, with no detached thread left: a test under TSan and ASan.
- A3 Lookups still deliver, and an abandoned lookup's late answer is never delivered: tests.
- A4 The suite passes, plus CI.

## Contracts

None changed for callers.

## Decisions

Filed by the lead 2026-11-18 from Xapiand's goal5-audit (G5-C1); cluster-gate waits on it.

## May land early

None.
