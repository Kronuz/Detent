# consensus-fetch-lane: journal

State: blocked on lead: A2 on slow CI runners (append supersede livelock, below); jobs at consensus:ce205c6: 261113-024645-d214ac 261113-024647-753d21 261113-024649-d90224 261113-024704-6e0b23 261113-024707-ff768c 261113-024710-f17f16 261113-024713-b5e0f8 261113-024715-24d7f0
Quest: .campaign/detent/quests/consensus-fetch-lane.md (the lead's; never edit it)

## Doing

CI red on macos-x86_64 at consensus:07077b9 (run 43409164420): `fetch-alongside-consensus`
commit failed at 2 s. Investigated; the lead's hypothesis (F1 closing a slow lane) is killed.

## Next

1. Lead decides the A2 option under "Blocked on".
2. Read the jobs at consensus:ce205c6 and update the Evidence table from `campaign brief`.

## Blocked on

Lead: what A2 asserts on slow runners. Findings:

- F1 is not involved. A2 runs with the default `fetch_deadline` (30 s), so the close needs 60 s,
  and the CI counters show all lanes intact: connections 4/4/4, admitted 4, rejected 0,
  reconnects 4/2/0 (the startup dials, same as local). CI at consensus:947a502 (before F1, run
  43406908950) failed too: linux-tsan saw the leader's term change during the fetches (commits up
  to 388 ms), macos-x86_64's A2 passed with commits up to 218 ms and round trips in the 64 ms
  bucket, then `fetch-failures` failed (that ordering changed in 07077b9).
- The CI failure: the leader and voter three committed index 12; voter two stayed at 11 for 2 s
  with its lanes up. Cause, reproduced locally without any fetch: the probe
  `~/.scratch/fetch-ci/probe-hog.patch` (job 261113-024715-24d7f0, not evidence) spins N ms per
  executor cycle. Commits: N=0 20 ms; N=1 ~65 ms; N=2 75-130 ms; N=5 none in 4 s, with 0
  append responses, 194 superseded and 194 unmatched. Once an append round trip exceeds
  `rpc_timeout` (40 ms in the test's `compact` timing), the Core re-sends, which supersedes the
  flight, and drops the late response as unmatched: no append ever completes. That is a
  livelock in the Core and transport's append matching, and it predates this quest. Fetch load
  on a slow runner only pushes round trips past 40 ms: all three voters, and the TLS for
  128 MiB, share one test thread.
- Options:
  (a) Fix the livelock in the Core: accept a late response that still matches the log (index
      and term), not only the current flight. That is core.h, outside this file set; a separate
      quest.
  (b) Run A2 under a timing whose rpc_timeout fits a slow runner, for example
      `{20, 200, 400, 800}`, and keep the 2 s commit bound, the no-timeout check and the
      unchanged-term check.
  (c) Pace the fetch lane across lanes: write a fetch frame only while no control lane of the
      voter has queued frames. This shortens but does not bound the cycle in a one-thread,
      three-voter test, and it gives up throughput.
  Recommendation: (a) as its own quest, since any slow executor livelocks consensus, and (b)
  here, so A2 measures what the fetch lane costs and not that cliff.

## Review findings (correctness, CHANGES at consensus:947a502)

- F1 (blocking) fixed in consensus:07077b9: a reply still unfinished one `fetch_deadline` after its
  fetch failed closes the bulk lane (`Peers::await_reply`), freeing the slots; both ends start over
  on the redial. Test `fetch-hung-peer`: voter two hangs for good with `fetches = 1`; the fetch
  times out at 300 ms, a second one is refused within the grace, the lane closes at 600 ms, and
  the next fetch gets `not_connected`; after voter two resumes, a fetch returns the bytes.
  The keep-hung-lane mutant (drop `owner->close(connection)`) survived at 07077b9 (job
  261113-022558-188050). The frozen peer had left part of a 64 KiB frame on the wire, so the
  frame deadline closed the lane at 2 s anyway. Fixed in consensus:ce205c6: voter two now hangs
  before the request reaches it. Checked locally: the fix closes the lane at 600 ms, and the
  mutant fails at the wait for the freed slot.
  Confirm mapping: F1 -> suite 261113-024645-d214ac (runs `fetch-hung-peer`), job
  261113-024647-753d21, mutant 261113-024649-d90224, all at consensus:ce205c6.
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
| A1 | 261113-024704-6e0b23 (codec + `fetch-exact`), suite 261113-024645-d214ac |
| A2 | 261113-024707-ff768c (`fetch-alongside-consensus`, local); red on CI macos-x86_64, see Blocked on |
| A3 | 261113-024647-753d21 (`fetch-failures`, `fetch-hung-peer`); fails without: 261113-024710-f17f16 (leak-pending-fetch), 261113-024649-d90224 (keep-hung-lane, F1) |
| A4 | 261113-024713-b5e0f8 (local macOS TSan); `ci:linux-tsan` red at 947a502 (term change, same cause) |
| A5 | [artifact] `CHANGELOG.md` 2026-11-13 "Added": the fetch lane entry |

At consensus:07077b9, all passed locally: suite 261113-022554-e164e1, A1 022615-385856,
A2 022622-b5fc07, A3 022555-f957a0, TSan 022602-a85d8e. The keep-hung-lane mutant
022558-188050 survived, and the leak mutant 022626-75ca1c was skipped.
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
