# consensus-fetch-lane: journal

State: ready for review (jobs queued at consensus:07077b9: 261113-022554-e164e1 261113-022555-f957a0 261113-022558-188050 261113-022602-a85d8e 261113-022615-385856 261113-022622-b5fc07 261113-022626-75ca1c)
Quest: .campaign/detent/quests/consensus-fetch-lane.md (the lead's; never edit it)

## Doing

The correctness review's fixes (review: .campaign/detent/reviews/consensus-fetch-lane-correctness.md,
CHANGES at consensus:947a502) are committed at consensus:07077b9; the jobs below are queued there.

## Next

1. Read the jobs at consensus:07077b9 and update the Evidence table from `campaign brief`.
2. The tsan gate `ci:linux-tsan` and CI need the branch pushed (the lead's call).

## Blocked on

Nothing of mine. CI and the tsan gate need a push (lead).

## Review findings (correctness, CHANGES at consensus:947a502)

- F1 (blocking) fixed in consensus:07077b9: a reply still unfinished one `fetch_deadline` after its
  fetch failed closes the bulk lane (`Peers::await_reply`), freeing the slots; both ends start over
  on the redial. Test `fetch-hung-peer`: voter two hangs for good with `fetches = 1`; the fetch
  times out at 300 ms, a second one is refused within the grace, the lane closes at 600 ms, and
  the next fetch gets `not_connected`; after voter two resumes, a fetch returns the bytes.
  Mutant keep-hung-lane (drop `owner->close(connection)`): job 261113-022558-188050.
  Confirm mapping: F1 -> suite 261113-022554-e164e1 (runs `fetch-hung-peer`), job
  261113-022555-f957a0, mutant 261113-022558-188050.
  `fetch-failures` now resumes its frozen peer before the second deadline (deadline 500 ms).
- F4 done: under TSan (`__has_feature(thread_sanitizer)` / `__SANITIZE_THREAD__`) the A2 bound
  is the 256 ms bucket; Release keeps 64 ms. The unchanged leader term carries the election bound
  in both. Exact maxima are not windowable in DurationHistogram, so the bucket stays.
- F7 done: `fetch-hung-peer` stops voter one with a fetch pending: `connection_aborted`, drained.
- F3, F5, F6 documented (fetch.h, docs/mechanisms/peer-fetch.md); behavior unchanged:
  - F3 is namespace-s5-3's on Detent's side: the reader runs on the owner executor for the whole
    file, so Detent serves history files from memory or measures append round trips while serving
    its largest file from disk.
  - F6 is namespace-s5-3's too: confine peer-sent names to history file names.
  - F5 follow-up: grow the reply buffer as chunks arrive instead of reserving the claimed total.
- F2 follow-up: a `fetches` mismatch closes the lane on the excess; options are the hello or
  profile fingerprint, or answering the excess with Failed. Detent uses the default on every voter.
- F8 follow-up (pre-existing): bulk-lane liveness (keepalive or a ping); a stale idle bulk lane
  also rejects a restarted peer's redial as a duplicate.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261113-022615-385856 (codec + `fetch-exact`), suite 261113-022554-e164e1 |
| A2 | 261113-022622-b5fc07 (`fetch-alongside-consensus`) |
| A3 | 261113-022555-f957a0 (`fetch-failures`, `fetch-hung-peer`); fails without: 261113-022626-75ca1c (leak-pending-fetch), 261113-022558-188050 (keep-hung-lane, F1) |
| A4 | 261113-022602-a85d8e (local macOS TSan); `ci:linux-tsan` pending a push |
| A5 | [artifact] `CHANGELOG.md` 2026-11-13 "Added": the fetch lane entry |

Earlier evidence at consensus:6c3018e: suite 261113-020036-ac250b, A1 020105-c7fbc8, A2
020109-c7e16e, A3 020113-47b337, mutant 020124-cd1754, TSan 020129-4b9750 (all passed).

## Result

consensus:6c3018e adds `Peers::fetch(peer, name, done)` and `PeerOptions::reader`
(`include/consensus/transport/fetch.h`, `peers.h`). DTFT0001 frames on the bulk lane, routed by
magic beside RFTXFR01 transfer frames; replies chunked at one lane frame (64 KiB body), each
charged to the shared byte budgets one frame at a time; fetch and snapshot frames alternate.
How it works and the numbers: consensus `docs/mechanisms/peer-fetch.md`.

A2 job 261113-020109-c7e16e: 4 rounds of 4 x 8 MiB fetches (128 MiB) in 540 ms through 1 MiB
budgets (high water 133 KiB); append round trips quiet 64 / fetching 64, all within the 16 ms
bucket, 0 timeouts; slowest commit 23 ms quiet, 41 ms fetching; leader term unchanged. A1 job:
24 concurrent fetches between three voters answered in 9 ms. A3 job: frozen peer fails at 300 ms
(the deadline), closed peer at 13 ms; the same lanes then fetch and commit.

Open: no fuzz target for `decode_fetch` (fuzz/ is outside the file set); no cancel frame (a
timed-out fetch drains its reply instead); Detent's pin bump is namespace-s5-3's.

## Rulings I made

- API: `FetchDone = void(std::error_code, std::optional<std::string>)`: error for no answer,
  nullopt for not-found (C++20, no std::expected). Errors are std::errc values (table in the doc).
- Reader lives in `PeerOptions::reader` (like `resolver`), not the PeerHost concept, so existing
  hosts compile unchanged; none answers not-found.
- A fetch that fails at its deadline (or exceeds `fetch_bytes`) keeps its slot and absorbs its
  late reply: no cancel frame, the lane stays open for one more deadline (then closes, F1), and
  the peer's queue bound (`fetches`) holds;
  a request past that bound or an unrequested reply closes the bulk lane. Voters must agree on
  `fetches`.
- The server reads lazily, when a request reaches the front, so one reply per lane is in memory.
  A reader that throws, or bytes over the server's `fetch_bytes`, answer Failed (`io_error`).
- Completions are posted (never inline) and not counted in `drained()`; they hold only `done`.
- "Existing bound" for A2 = the election minimum (100 ms in the test's timing): the test asserts
  every append round trip during the fetches lands in the <= 64 ms bucket, none times out, and
  the leader keeps its term (256 ms bucket under TSan, F4). Measured with the transport's own
  AppendTimings bucket deltas.
- Tests live in peers_test.cc/codec_test.cc (CMakeLists.txt is outside the file set); the test
  cluster gained an optional voter on its own io_context to freeze a peer mid-reply.
