# authority-pages-design: Design: should the authority build its own trie and pages, and serve pages or records by range

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status authority-pages-design`.

## Outcome

Owner ask (2026-11-21): weigh whether the authority should build its own trie and pages, so that the authority and relays both serve pages or records depending on the range asked for, and whether that is the optimal mode. Today the authority builds no pages (docs/design/dense-distribution.md D1, Limit 1, Q4 ruled (b)): relays build, agents verify against the relay's root, and a relay-less deployment gets records and builds.

The design answers with measurements on the 100K and 5M corpora (dense-pages' model and fixtures), not argument:
1. **Cost on the authority**: CPU, memory and bytes written to keep pages current per committed batch, on the leader and on each voter, and its effect on commit latency and election timing under the rc3 write load. Options to price: (a) no pages (today); (b) the leader builds; (c) every voter builds; (d) the authority keeps only the root commitment (incremental hashing, no stored pages).
2. **What it buys**: Limit 1 closed (the root is the authority's, so a relay that mis-applies records is caught); relay-less deployments get page transfers; one serving rule for authority and relays (pages or records by range, D6); relays may stop building (pass-through) or keep building as a cache.
3. **Failure and recovery**: a voter's pages after restart, snapshot and catch-up; what happens to pages at a leader change.
4. A recommendation with its numbers, the decision rows it would change (D33, D34, Q4) and the quests it would add or drop. The owner decides.

## Acceptance criteria

- A1 [artifact] docs/design/authority-pages.md prices options (a)-(d) with measured CPU, memory, write bytes and commit-latency deltas at 100K and 5M, each number citing its job
- A2 Each measurement is a strict or exclusive job at a named commit, before and after, with its noise floor: jobs
- A3 [artifact] The recommendation names what Limit 1, Q4 and D6 become and the quests added or dropped: docs/design/authority-pages.md
- A4 [manual] The owner's decision on the recommendation

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>
