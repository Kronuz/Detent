# consensus-fetch-lane: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-fetch-lane.md (the lead's; never edit it)

## Doing

Nothing: every job passed at consensus:6c3018e (the head, consensus:f51bf15, adds only the doc's
job numbers).

## Next

1. Correctness review (the lead launches it).
2. A4's gate is `ci:linux-tsan` on GitHub CI: it needs quest/detent/consensus-fetch-lane pushed,
   which is the lead's call. The local TSan job passed meanwhile.

## Blocked on

Nothing of mine. The tsan gate and CI need the branch pushed (lead).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261113-020105-c7fbc8 (passed at 6c3018eb6a6a; ignores docs-only docs/mechanisms/peer-fetch.md) |
| A2 | 261113-020109-c7e16e (passed at 6c3018eb6a6a; ignores docs-only docs/mechanisms/peer-fetch.md) |
| A3 | 261113-020113-47b337 (passed at 6c3018eb6a6a; ignores docs-only docs/mechanisms/peer-fetch.md); fails without: 261113-020124-cd1754 |
| A4 | 261113-020129-4b9750 (passed at 6c3018eb6a6a; ignores docs-only docs/mechanisms/peer-fetch.md); `ci:linux-tsan` pending a push |
| A5 | [artifact] `CHANGELOG.md` 2026-11-13 "Added": the fetch lane entry |

Suite: 261113-020036-ac250b passed (24/24 tests, peers 16.8 s of its 30 s ctest timeout).
Mutant: removing `fail(fetching, std::errc::connection_aborted)` from `close()` aborts at
peers_test.cc:1181 (the closed fetch never completes): killed.
Local TSan (llvm clang, macOS): 8/8 transport tests clean, peers 18.6 s.

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
  late reply: no cancel frame, the lane stays open, and the peer's queue bound (`fetches`) holds;
  a request past that bound or an unrequested reply closes the bulk lane. Voters must agree on
  `fetches`.
- The server reads lazily, when a request reaches the front, so one reply per lane is in memory.
  A reader that throws, or bytes over the server's `fetch_bytes`, answer Failed (`io_error`).
- Completions are posted (never inline) and not counted in `drained()`; they hold only `done`.
- "Existing bound" for A2 = the election minimum (100 ms in the test's timing): the test asserts
  every append round trip during the fetches lands in the <= 64 ms bucket, none times out, and
  the leader keeps its term. Measured with the transport's own AppendTimings bucket deltas.
- Tests live in peers_test.cc/codec_test.cc (CMakeLists.txt is outside the file set); the test
  cluster gained an optional voter on its own io_context to freeze a peer mid-reply.
