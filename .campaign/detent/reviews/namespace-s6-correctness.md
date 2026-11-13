# namespace-s6 review: correctness

Commit: Detent:38434738
Verdict: CHANGES
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261112-122211-8ba8d6 (native, full ctest at 13d2aa25; 38434738 adds only the journal): the
  suites for A1-A5, including test_distribution_session_frames, test_distribution_grants,
  test_delivery_session_request and test_delivery_namespace_session, and the unchanged producer
  and `/v2/result` suites (A4).
- 261112-122223-334801 (A1 mutant: the trim filter removed, the frames test fails).
- 261112-122212-f2e389 (A2 grep) and 261112-122212-c1a91a (pins).

## What I checked myself

Probe job 261112-132408-7c1fe9 (Debug, ASan+UBSan, fresh checkout), scripts in
`.scratch/review-namespace-s6-correctness/` (`run.sh` adds `probe_frames.cc` and
`probe_client.cc` as targets; rerun with the command in its header). Its exit 1 is the F2 probe.

1. Exhaustive tiers (risk 2): a 7-position churn with live deletes and tombstones of absent keys,
   every cut p <= ring start s <= 7, every tiling of (p, s] (named prefix, unnamed suffix, incl. no
   spans and an empty ring), every R in 0..8, own and foreign incarnation: 18,432 cases. Every tier
   matched D6's table (R at the cut, R = s, R = h, R > h, R < p, R = 0 under the same identity),
   the encoder in 1-byte steps produced the same bytes as one step, the body decoded in 3-byte
   chunks, and applying it to the state at R rebuilt the head with its revisions. No drop and no
   duplicate within a piece. Holds.
2. Hostile and truncated bodies (risk 1): all 273 strict prefixes of a body with a directory and
   a blocks frame are refused by `finish()`; a trailing value is refused; 200,000 random 1-3 byte
   mutations and truncations fed in 1-7 byte chunks: no crash, no sanitizer report. Allocation
   before R8's ledger admission is bounded by `Limits` (frames and directory entries <=
   namespaces, string bytes and records counted, 16 KiB token cap, overrides <= 256); piece counts
   allocate nothing. One wrap defect (F2).
3. Cursor move (risk 3): `NamespaceCursor`/`DirectoryCursor` keep the removed copies' fields, order
   and defaulted `==`; `agent_store.hh` re-exports them; `same_identity` is new and compares
   cluster and incarnation only. The W5 parser and client body carry the cursor as
   [name, incarnation, position] under the request's cluster; the round trip is in
   test_session_request and request_bodies(). Holds.
4. Grants (risk 4): code read against test_grants.cc, which covers `p.*` vs `pq`/`p`/`p_x`/`p-x`/
   `p/x`, nested prefixes, `*`, exact names, empty grants, 13 malformed forms and the 4,096 bound.
   Holds.
5. Client (risk 5): a 179 KB, 3-fragment next response whose last fragment fails SHA-256: the
   decoder throws `distribution fragment checksum mismatch` after the sink saw `frame()`, 1 piece
   and 4,395 of 6,000 records, without `end_frame()` (F3). Idle (204), expiry (410) and forbidden
   (403) never construct the frames decoder, so the sink sees nothing.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A younger named span sent whole is labeled piece kind 3 (Span), not 2 (Block): `whole = span.first == answer.after + 1` is true only for the span holding R + 1. `docs/status/namespace-s6.md` and `session_frames.hh` define kind 2 as "a named span sent whole, fetchable by (namespace, incarnation, first, last)", and the blocks tier as "each younger named span whole". In 2,561 of 4,353 cases where a whole named span lies above R, the wire calls it a Span. Journal R5 states the code's rule, so the provided `namespace-session` contract says two things, and S8's relays (block reuse) build against it. | probe 261112-132408-7c1fe9 (`exhaustive cases ... labeled Span instead of Block: 2561`); `src/detent_distribution/session_frames.cc:168` | Pick one meaning. Recommended: `whole = span.first > answer.after`, so every named span sent whole is kind 2, matching the doc. Otherwise, reword the doc, header and R5 to say kind 2 means only the block starting at R + 1. Either way, the tiers test asserts the kind of every named span piece (e.g. R = 8: [9,16] and [17,24] both Block). |
| F2 | no | defect | The decoder accepts pieces after a frame reaches a target of 2^64-1: `at + 1` wraps to 0, so a further Span [0, max] passes the "next position" check. Its records at lower revisions reach the sink, which ends with `a:x` at max-1 after applying max. Unreachable from an honest authority, but it breaks the documented "pieces run contiguously to the target". | probe 261112-132408-7c1fe9 (`wrap at 2^64-1: ACCEPTED`); `session_frames.cc:469,477,479` | Refuse a piece once `at == to.position` (`wire(at < to.position, ...)` in `piece_header_done`), and add the body to test_session_frames' hostile table. |
| F3 | no | gap | A corrupt later fragment fails the call after the sink has applied part of a frame. The header calls the frames provisional ("a frame counts once next returns Frames"), but the A3 test's `Store` applies records and moves its cursor in `frame()`. Its corrupt case is a single fragment, so the test never shows the staging that contract needs. The cutover's sink owns this. | probe 261112-132408-7c1fe9 (`records 4395 ended 0`); `tests/detent_delivery/test_namespace_session.cc:228-262,357-363` | In this quest or the cutover: make the test `Store` stage and commit only on `FramesOutcome::Frames`, and corrupt a multi-fragment response, checking the store is unchanged afterwards. |
| N1 | no | nit | `detent::authority::NamespaceCursor` (`src/detent_authority/state.hh:83`) is still a second W1 cursor. The A2 grep allows it as namespace-s6-cutover's; that quest's file should name its removal. | job 261112-122212-f2e389 command | Name it in namespace-s6-cutover's criteria. |
| N2 | no | nit | `records()` skips trimmed records (revision <= R) without counting them against `bytes`, so one `step` can read a whole large block when R + 1 is near its end. Its work is bounded by the span, not the step. | `session_frames.cc:270-283` | Count skipped records toward the step bound before the cutover puts it on the authority's loop. |
| N3 | no | nit | Names may hold empty components (`is_name_char` allows any '.'), so `p.*` grants `p.` and `p..x`. That is consistent with the charset, but not obviously "below `p.`". | `src/detent_distribution/grants.cc:36` | Keep it, or define name components and refuse empty ones at the authority. |

## Not run

- The other platforms and the TSan/ASan CI matrix (CI's; running at 38434738). My ASan/UBSan
  run covers only the two probe binaries.
- The client against a live server over TLS beyond the suite's test; corrupt compressed (zstd)
  multi-fragment responses.
- The W5 parser beyond reading `parse_positions` and the suite's round trip; no fuzzing of it.
- Directory semantics (duplicate names in one update are accepted by both sides; D5 ordering is
  the cutover's).
