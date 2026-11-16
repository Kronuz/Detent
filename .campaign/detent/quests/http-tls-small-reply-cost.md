# http-tls-small-reply-cost: Attribute and cut TLS small-reply CPU at one connection and the close path

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status http-tls-small-reply-cost`.

## Outcome

The +3.2 us CPU per small TLS keep-alive reply at one connection and the 3% slower TLS close path that http-idle-cost left (D103, http:ad6771f against a17b197) are attributed by a profile and either removed or explained as the price of releasing idle buffers, with the idle-memory win kept.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 The cost is attributed: an exclusive profile (sample or per-request counters) of TLS keep-alive c1 and TLS close c8, base vs head, names where the extra CPU goes, recorded in the journal with job ids
- A2 Each named cost is removed, or kept with a measured reason: an exclusive load A/B with A/A puts TLS c1 CPU per request and TLS close c8 req/s inside their A/A bands, or the journal states the remaining cost per request and why
- A3 The idle win holds: 10K TLS idle bytes per connection within 1% of http-idle-cost's 40,054 B: an exclusive job
- A4 The suite and TSan pass: the suite check and a TSan job

## Contracts

No API change.

## Decisions

D103 (owner): follow-up to http-idle-cost's accepted trade.

## May land early

Measurement jobs and bench changes.
