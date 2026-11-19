# prism-server-composition-limits: Prism: the composition steps that wait on http::Limits, reactor's server TLS and Http::stop_accepting

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status prism-server-composition-limits`.

## Outcome

Prism finishes the server composition note's three steps that prism-server-composition could not take before their libraries landed: limits through `http::Limits` (Xapiand http-limits), `raise_open_files` and `tls::server_context` from reactor (reactor-server-composition), and stopping only the listeners on drain (`Http::stop_accepting`).

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 Prism's limits come from `http::Limits` and its TLS context from `tls::server_context`, with no local copy of either: a test or build job.
- A2 A drain stops accepting while in-flight requests finish: a test that fails with the old full stop.
- A3 [artifact] ARCHITECTURE.md's composition row lists no remaining deviation for these three steps.
- A4 The suite passes, plus CI.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
