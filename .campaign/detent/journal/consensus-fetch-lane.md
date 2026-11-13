# consensus-fetch-lane: journal

State: waiting on jobs 261113-020036-ac250b 261113-020105-c7fbc8 261113-020109-c7e16e 261113-020113-47b337 261113-020124-cd1754 261113-020129-4b9750
Quest: .campaign/detent/quests/consensus-fetch-lane.md (the lead's; never edit it)

## Doing

Jobs at consensus:6c3018e: suite, A1-A3 scenarios, A3 mutant, local TSan (A4 support).

## Next

1. Read the jobs; paste `campaign brief`'s table; set "ready for review".
2. A4 proper is `ci:linux-tsan` on GitHub CI: needs the branch pushed (the lead's call; I never push).

## Blocked on

Nothing. A4's CI check needs the lead to push quest/detent/consensus-fetch-lane.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | job 261113-020105-c7fbc8 (codec + `peers_test fetch-exact`), suite 261113-020036-ac250b |
| A2 | job 261113-020109-c7e16e (`peers_test fetch-alongside-consensus`) |
| A3 | job 261113-020113-47b337 (`peers_test fetch-failures`); mutant 261113-020124-cd1754 (drop the connection_aborted fail in close(): passes iff killed) |
| A4 | `ci:linux-tsan` (needs push); local macOS TSan job 261113-020129-4b9750 |
| A5 | [artifact] `CHANGELOG.md` 2026-11-13 "Added": the fetch lane entry |

## Result

consensus:6c3018e adds `Peers::fetch(peer, name, done)` and `PeerOptions::reader`
(`include/consensus/transport/fetch.h`, `peers.h`). DTFT0001 frames on the bulk lane, routed by
magic beside RFTXFR01 transfer frames; replies chunked at one lane frame (64 KiB body), each
charged to the shared byte budgets one frame at a time; fetch and snapshot frames alternate.
How it works and the numbers: consensus `docs/mechanisms/peer-fetch.md`.

Local run (not evidence, i9-9980HK, release): 4 rounds of 4 x 8 MiB fetches in 511 ms through
1 MiB budgets (high water 133 KiB); append round trips quiet 64 / fetching 62, both within the
16 ms bucket, 0 timeouts, leader term unchanged. Whole peers test ~16 s (ctest TIMEOUT 30).

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
