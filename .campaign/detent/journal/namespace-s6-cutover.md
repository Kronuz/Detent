# namespace-s6-cutover: journal

State: working
Quest: .campaign/detent/quests/namespace-s6-cutover.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1: an authority `NamespaceHistory` over S5.2's layout table, history files, ring and the newest
seal's state (`src/detent_authority/namespace_history.{hh,cc}`), then the v2 session endpoints in
`service.cc` on it.

## Next

1. A1 adapter + test (bytes per answer vs `Layout::answer` and an oracle).
2. v2 open/next/close in the authority over `NamespaceGrants` (A3), W7 full copy (A2).
3. A4-A7 per the lead's answer to msg-49.

## Blocked on

Lead (msg-49): A4-A7 need files outside the set (delivery, distribution, distribution_http, relay,
detent, testing, system tests, docs); split (recommended) or widen. A1-A3 proceed in set.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
