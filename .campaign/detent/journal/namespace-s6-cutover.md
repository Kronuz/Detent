# namespace-s6-cutover: journal

State: working
Quest: .campaign/detent/quests/namespace-s6-cutover.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1-A3 in set: `AuthorityHistory` (namespace_history), `FramesExport`, `NamespaceSessions`, the v2
session path in service.cc (ea9e2abb). Tests: test_authority_namespace_history (unit, three
voters, W7), test_authority_frames_service (HTTP end to end vs an oracle from the voter's disk).

## Next

1. Build and run test_authority_frames_service; commit; jobs for A1 (both tests + a mutant), A2,
   A3 (tests + grep), and the authority suite.
2. A4-A7 per the lead's answer to msg-49.

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

- v2 answers come from applied, committed state on the owner, no quorum read: a session answer is
  a cache fill that a later next corrects, like the relay's.
- The whole answer is encoded into ledger-admitted 64 KiB chunks before fragmenting: DTDIST01's
  header carries the total. An answer past `delivery.bytes` is 503 before any header.
- The full copy is read from the sealed state, not merged files; the unit test proves equality with
  `Layout::answer`'s full copy.
- History files collected mid-answer raise HistoryFailure: 503, retryable.
- A next re-looks when `applied_index` moves, so seals and log progress both wake it.
- The enabling entry is proposed lazily by the leader when a v2 next needs positions (at most once
  a second), so v1 keeps working until A4 removes it; the cutover moves it to leader start.
- The v1 `AuthorizedScope` exact-set check (export_scope.cc) serves only the v1 export; it goes with
  v1 in A4. A3's grep then covers it; until then v2, /v2/transactions and /v2/read use W13 grants.
