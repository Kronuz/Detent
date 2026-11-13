# namespace-s6-cutover: journal

State: ready for review
Quest: .campaign/detent/quests/namespace-s6-cutover.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1-A3 per D93 (msg-49 answered: split; A4-A7, AuthorizedScope removal and the agent's table-entry
shape are namespace-s6-v1-removal). Correctness review CHANGES at 05ec8079 folded in at 8d667269:

- F1 (blocking): `select_answer` gives a same-cluster store above head Ahead whatever its
  incarnation (session_frames.cc). Tests: test_authority_lagging_incarnation (the reviewer's probe,
  now requiring Ahead and no frame), test_session_frames `other_incarnation`.
- F2 (blocking): test_authority_frames_service opens `all` under the exact `ns` grant and checks
  that only `ns` appears in the directory and the frames.

## Next

1. Lead: confirm F1=261113-094044-ae3ff3 F2=261113-094044-ae3ff3 (the native suite at 8d667269)
   once it and the mutant chain finish.
2. msg-50 (lead): history.hh's tier comment (outside my set) to match F1.

## Blocked on

Nothing.

## Evidence

Jobs at Detent:8d667269 (submitted; not yet finished when I ended the turn):

| Criterion | Evidence |
|---|---|
| A1 | 261113-094020-d3c6fb; fails without: 261113-094032-0c4de6 (no gap hold-back), 261113-094036-8d045d (blocks as sections: byte compare) |
| A2 | 261113-094020-d3c6fb; fails without: 261113-094023-df33a1 (F1: identity before position) |
| A3 | 261113-094020-d3c6fb, grep 261113-094047-3ffa30; fails without: 261113-094027-e8afb8 (F2: `all` ignores grants), 261113-094041-4f9f7d (follows ignore grants) |
| suite | 261113-094044-ae3ff3 (native, fresh checkout) |
| pins | 261113-094051-f35e53 |

Review confirm: F1 -> 261113-094044-ae3ff3, F2 -> 261113-094044-ae3ff3.

Earlier, at 05ec8079: A1-A3 261113-091246-eab613, mutants 261113-091251-d1a00c, -091258-d2d9be,
-091559-6bf109, -091305-4832aa; suite 261113-091333-9c4789. Review probes (lead's reviewer, not
mine): 261113-092941-f5f614, 261113-093054-05276f (F1 present at 05ec8079), 261113-093301-a65e2c
(F2 mutant survived at 05ec8079).

## Result

The authority answers W5 sessions from S5.2's history per namespace, chosen by the cut (A1); a
re-created namespace arrives as a full copy under its new cursor, and a lagging voter never sends
the dropped incarnation to a store already past it (A2, F1); every v2 path checks W13 grants,
`all` included (A3, F2). How it works: docs/status/namespace-s6-cutover.md. Measured: the HTTP
test answers 46 cursors (11847 bytes) byte for byte against the disk oracle; the unit test 652
cursors byte-identical on three voters.

Follow-ups from the correctness review (not blocking; for the lead to place):
- F3: the whole answer is buffered in the delivery ledger (default 128 MiB) before headers, below
  `delivery.bytes` (256 MiB) and shared with ring captures; count-then-stream like v1, or cap an
  answer's share. Not measured.
- F4: one answer slot per voter; a next whose wait elapses while another answer streams gets 204
  without being looked at. Answer 503 busy, or look without encoding.
- F5: moved to namespace-s6-v1-removal A6 (lead).
- F6: the Entries tier is thin (4 answers unit, 1 HTTP); add a phase with a larger seal threshold.
- F7: any credential's v2 next enables positions; moves to leader start in v1-removal.
- F8: `all` with only ungranted directory changes gets an empty answer and never advances its
  cursor; send the directory update even without granted entries.

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
- F1's fix sits in `select_answer` (shared by the relay), not in service.cc: it is the contract's
  rule (D1, positions continue across incarnations), so every source gets it.
- Version 1's grant view keeps exact names only (service.cc), so W13 patterns narrow v1, never
  widen it, until v1-removal deletes it.
