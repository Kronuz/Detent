# namespace-s6-cutover: journal

State: blocked on lead: msg-49 (A4-A7); A1-A3 done at 05ec8079, suite job 261113-091333-9c4789 running
Quest: .campaign/detent/quests/namespace-s6-cutover.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1-A3 done at 05ec8079: `AuthorityHistory` (namespace_history), `FramesExport`,
`NamespaceSessions`, the v2 session path in service.cc. Tests: test_authority_namespace_history
(three voters, 652 cursors byte-identical, W7), test_authority_frames_service (HTTP, 46 cursors
byte for byte vs an oracle from the voter's disk table and files; W13; statuses).

## Next

1. Read suite job 261113-091333-9c4789 (native, regressions); fix anything it shows.
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
| A1 | 261113-091246-eab613; fails without: 261113-091251-d1a00c (no gap hold-back), 261113-091258-d2d9be (full-copy revision +1, 503), 261113-091559-6bf109 (blocks as sections: byte compare) |
| A2 | 261113-091246-eab613 (test_authority_namespace_history W7 check) |
| A3 | 261113-091246-eab613, grep 261113-091326-a38a94; fails without: 261113-091305-4832aa (grants ignored: 403 check). Partial: v1's `AuthorizedScope` exact-set check goes with v1 (A4) |
| A4 | blocked on msg-49 |
| A5 | blocked on msg-49 |
| A6 | blocked on msg-49 (authority side admits through the delivery ledger: FramesExport chunks, NamespaceSessions maps) |
| A7 | blocked on msg-49 |
| pins | 261113-091339-3e4818 |

## Result

Partial (A1-A3). The authority answers W5 sessions from S5.2's history per namespace, chosen by the
cut; docs/status/namespace-s6-cutover.md says how. Open: A4-A7 (msg-49).

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
