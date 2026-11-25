# namespace-s6-cutover review: correctness

Commit: Detent:05ec8079219d31c4f0b27067b63ac64c4fde2513
Verdict: CHANGES
Date: 2026-11-13

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Scope: A1-A3 per D93 (v1 code still present is expected).

## Evidence I relied on

- 261113-091246-eab613 (A1-A3 tests pass), 261113-091333-9c4789 (native suite, exit 0): no regression.
- Mutants 261113-091251-d1a00c (gap hold-back), 261113-091258-d2d9be (full-copy revisions),
  261113-091559-6bf109 (blocks as sections), 261113-091305-4832aa (followed names ignore grants):
  each kills its mutation, so the hold-back, the full-copy bytes, the block/section distinction and
  the follows grant check are load-bearing in the tests.
- 261113-091326-a38a94 (grep): no `namespaces.contains/find/count` left in `src/detent_authority`.

## What I checked myself

Scripts under `.scratch/review-namespace-s6-cutover-correctness/` (home checkout), rerunnable at a fix:

- `probe.sh` + `probe_lagging_incarnation.cc`, job 261113-093054-05276f (exit 1 = defect present;
  exit 0 once fixed). It also prints the two A1 tests' stdout, which ctest hides:
  three voters, 652 cursors: 519 full copies, 71 blocks, 4 entries, 58 frameless, a held-back head
  in 1 answer; HTTP: 46 cursors: 35 full, 8 blocks, 1 entries, 2 idle; cut 33, ring from 41, head 42.
- `mutant_all_filter.sh`, job 261113-093301-a65e2c: removing the grant filter on `all`'s directory
  walk (service.cc:2530) leaves every test passing (SURVIVED; exit 0 once a test kills it).

By reading, against the risks named:

1. A1 tiers. `AuthorityHistory::freeze` takes p = base.last, s = the table's position, spans =
   blocks then level-0, all from the in-memory table at the latest seal, checked against the sealed
   state (namespace_history.cc:186-205); new incarnations enter the table at their first seal
   (history_layout.cc:118-133), so a re-created namespace never sticks at its origin. The answer is
   frozen (table copy, ring `shared_ptr`s, sealed `State` copy, immutable digest-checked files), so
   a seal, roll-up or collection between loop turns yields the frozen answer or a 503, never a mix.
   The oracle is independent enough: the three-voter test ends every answer at `store_at`, built
   from the producer's own writes; the HTTP oracle reads the table and files from disk and the ring
   from the producer's writes. Tier choice is `select_answer` in both oracle and code (shared), but
   the unit test re-derives it from cut and ring start. All R from 0 to h + 1 are enumerated, so the
   cut and ring boundaries are covered; zstd is checked on one full copy (the coding is the fragment
   layer's).
2. F5. A capture gap trims the ring to the contiguous run above s and reports the head there
   (namespace_history.cc:206-214); a store above it is Ahead (no frame). Nothing is skipped; the
   client sees only a lower head (metric `frames_held_back`). Acceptable as ruled.
3. A2. A store of the dropped incarnation, at its head or at the new head, gets a full copy of the
   new one (test W7 block). The converse fails on a lagging voter: F1.
4. A3. `/v2/transactions` and `/v2/read` check `grants.allows` before preparing (service.cc:853,
   875); open and next set 403 before queueing and before any freeze; close reads nothing;
   `/v2/result` returns the producer's own outcomes; v1 export and subscriptions keep exact valid
   names only (service.cc:621-622), so patterns narrow v1 and never widen it. No ungated path found;
   the `all` filter is gated in code but untested (F2).
5. Lazy enabling entry. Apply is idempotent (a second EnablePositions is NoChange, state.cc:1871-
   1873); answers read only applied (committed) state; the proposal is leader-only and at most once
   a second, and a lost or duplicated proposal across a leader change or restart is re-proposed or
   no-ops. No inconsistency or early serve found.
6. Resource bounds. Sessions are capped (total and per principal), requests by admission, one answer
   in flight per voter whose deadline is delivery_timeout + wait_ms; memory is bounded by the
   delivery ledger. See F3 and F4 for what that bound costs.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A voter whose apply lags a drop and re-creation sends a store already at the new incarnation a full copy of the dropped one: the namespace goes backwards (D1) and readers switch to deleted data until a later next. `select_answer` compares identity before position, and v2 is served from applied state on every voter with no read barrier (the journal's ruling), so a follower or a just-elected leader reaches it. Positions continue across incarnations (state.cc:932-937, 1906-1918), so the store's position is above the lagging head. | Job 261113-093054-05276f: leader head (new incarnation, 12), lagging voter head (old, 10), answer FullCopy, 157 bytes. session_frames.cc:35-36 | A same-cluster store whose position is above the voter's head is Ahead whatever its incarnation: in `select_answer`, or in service.cc's `answer` if the quest keeps to its file set. Test: `probe_lagging_incarnation.cc` as a suite test (exit 0). |
| F2 | yes | gap | The W13 filter on `all` (directory entries and the full copies they bring) is untested: the only `all` session uses a `*` credential, so deleting the check passes every test. It is the one v2 path that hands out namespaces the request never named. | Job 261113-093301-a65e2c (SURVIVED); service.cc:2530; test_frames_service.cc `watcher` | A narrow-grant session with `all` (e.g. `reader`, granted `ns`) whose answer has no `d.x` entry or frame; `mutant_all_filter.sh` then exits 0. |
| F3 | no | gap | The whole answer is buffered in the shared delivery ledger before headers (frames_export.cc:28-52), unlike v1, which counts and then streams (delivery_export.cc:107-160). The ledger defaults to 128 MiB (service.hh:47), below `delivery.bytes` 256 MiB (protocol.hh:14), and also holds the ring's captures (D6 3). An answer above the ledger's free space fails 503 on every retry, and a large answer to a slow reader, held until its deadline, makes ring captures fail meanwhile, holding heads back on that voter until the next seal. Not measured. | frames_export.cc:28-52; service.hh:47; protocol.hh:14 | Count, then stream as v1 does, or cap an answer's share of the ledger; a test with an answer above that share. |
| F4 | no | gap | One answer slot per voter: while it streams, any next whose wait has elapsed gets 204, "nothing changed", without ever having been looked at. A slow reader delays every other session until its deadline. | service.cc frames loop (`if (!framing && task.looked != ...)` then `now >= task.poll_deadline` → 204) | Answer 503 busy for a next never looked at, or look without encoding. |
| F5 | no | gap | A followed namespace that is dropped and not re-created gets no frame (`freeze` returns null), so a session without `all` never learns of the drop and keeps the deleted data. A live namespace that cannot be frozen (table not at the sealed state) is also omitted silently, while `all`'s directory still advances. A contract question for namespace-session: a drop frame, or 503 for a live namespace that cannot be frozen. | namespace_history.cc:186; service.cc `answer` (`if (auto frozen = freeze(name))`) | Owner's or lead's call; keeping it as is is an option. |
| F6 | no | gap | The Entries tier (R in [s, h)) gets 4 answers in the three-voter test and 1 over HTTP, because seals every 6 and 3 transactions leave rings of 1 to 5 entries. Multi-entry ring reads are covered inside full and block answers. | Job 261113-093054-05276f stdout | A phase with a larger seal threshold, so R falls inside a ring of many entries. |
| F7 | no | nit | Any authenticated credential, including `publish: false`, enables positions irreversibly with one v2 next; v1 after enabling is not tested through the service. It is greenfield, and the ruling moves the entry to leader start in v1-removal. | service.cc enabling block (`wanted && ... Leader`) | None now; carry it to namespace-s6-v1-removal. |
| F8 | no | nit | `all` whose only directory changes are ungranted gets an empty answer, so its cursor never advances and each next rescans those entries; `to.position` reveals how many ungranted changes happened. | service.cc `answer` (`update.from < update.to.position`) | Send the directory update even with no granted entries. |

## Not run

- Sanitizers (TSan or ASan) and the Linux matrix: CI's, still running at 4918676835bf.
- A large (multi-turn) answer with a seal, roll-up and collector running mid-answer: argued by
  construction above (frozen view, 503 on a missing file), not exercised.
- Leader change and restart around the enabling entry: argued from apply's idempotence, not run.
- F3's memory interaction: not measured.
