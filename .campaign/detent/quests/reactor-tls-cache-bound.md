# reactor-tls-cache-bound: reactor: the TLS parse cache releases entries no context uses

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reactor-tls-cache-bound`.

## Outcome

The parse cache in `tls::configure` holds no entry, and no decrypted key, once every context built from it is gone, so the cache is bounded by the live contexts (reactor-tls-cache review F3).

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 After the last context using an entry is freed, the entry and its key are released: a test that fails without it.
- A2 Configuring N distinct paths and freeing their contexts leaves the cache empty: a test.
- A3 The suite, ASan+UBSan and TSan pass: jobs.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
