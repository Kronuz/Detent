# consensus-fetch-lane review: correctness

Commit: consensus:947a5020d68976453dea5e2a74e32f4a69b76993
Verdict: CHANGES
Date: 2026-11-13

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Suite 261113-020838-86abf7 at consensus:947a502: 24/24, peers 16.2 s. It runs F1-F3 and the codec
  test with asserts live (`-UNDEBUG`, CMakeLists.txt:117).
- Decoder fuzz 261113-020841-ed49d4 at consensus:947a502: `FUZZ_DECODER=8`, 46.5M runs in 601 s,
  no crash. The harness requires that `encode_fetch(decode_fetch(x)) == x`.
- A1-A4 jobs 261113-020105-c7fbc8, 020109-c7e16e, 020113-47b337 and 020129-4b9750 ran at
  consensus:6c3018e. The code at 947a502 is the same; 947a502 adds only the doc and the fuzz
  harness. Mutant 261113-020124-cd1754 removes `fail` from `close()` and is killed.

## What I checked myself

The probe scripts are in `.scratch/review-consensus-fetch-lane-correctness/`. `inject.py` adds the
probe scenarios in `probes.inc` to a fresh checkout's `peers_test.cc` and leaves the branch alone.

- 261113-021451-c6bcfc (`behavior.sh`, Release):
  - `review-stuck`: voter two hangs after four fetches reach it. Then 9 of 10 fetches over 5 s are
    refused at once with `resource_unavailable_try_again`. `occupancy.fetches` stays at 4 and the
    connections stay the same. The slots drain only after the peer resumes.
  - `review-mismatch`: the requester has `fetches=8` and the server has the default 4. Six
    concurrent fetches all fail with `connection_aborted`, the server's `stats.rejected` goes up by
    1, and the lanes reopen.
- 261113-021453-acea31 (`a2-margin.sh`): TSan build, `fetch-alongside-consensus`. It ran 5 times
  idle and 5 times beside 4 CPU burners, and passed all 10. Under TSan the slowest append round
  trip is in the (16, 64] ms bucket in every run; Release puts it in the <= 16 ms bucket. The
  slowest commit was 73 ms and no run had a timeout. Budgets peaked at 130 KiB.
- I read the code for:
  - Decoder totality (fetch.h:56-69).
  - Every completion path: `fetch` (peers.h:215), the deadline (peers.h:231), `received`,
    `close` (peers.h:325-339) and `stop`.
  - The writer's alternation and credit (peers.h:754-775).
  - Detent's seam: `history::Fetch` and `Directory::find` in namespace-s5-3.

## Answers to the lead's questions

1. `decode_fetch` is total. It checks the length before it indexes, then the magic, the zero
   padding, the kind, and every field through `valid_fetch`; the fuzz job above agrees.
   - The requester rejects a reply to an id it never asked for, a chunk past `total`, a `total`
     that changes, and Missing or Failed after Data. Each one throws, which closes the bulk lane.
   - A flood of Requests past `fetches` closes the lane, so a server holds at most `fetches` names
     of 64 KiB each.
   - A chunk carries no offset, so "out of order" cannot be expressed. A duplicated chunk passes the
     transport, and the host's digest has to catch it (Detent checks one).
   - Allocation: see F5. Executor time: the serving voter runs the reader on the owner executor, for
     the whole file (F3).
2. Every path completes a fetch exactly once, on the executor:
   - `complete` posts the completion. `fail` uses `std::exchange` on `done`, so a second call does
     nothing.
   - The deadline handler finds its fetch by a unique id. A handler that is already queued after a
     reply or a close finds nothing.
   - `close` cancels the timers, fails every fetch, then clears them. It is idempotent across the
     reader and writer catches, `run` and `stop`.
   - F1 and F3 check that a completion runs once, on the executor, and never inside `fetch`.
   - No test covers a local `stop()` with fetches pending (F7).
3. With a live, correct peer, keeping the slot is safe: a server's `serving` entry leaves when its
   last frame is encoded, before the requester erases `fetching`, so equal limits never trip the
   bound. It is not safe when a reply never comes (F1). A mismatch in `fetches` is only documented,
   not detected (F2).
4. A2 bounds what the transport costs consensus: all three voters run on one thread, and round trips
   stay <= 16 ms in Release and <= 64 ms under TSan, including on a loaded host. It does not bound
   the reader's cost (F3). The 64 ms assertion sits on a bucket edge that TSan already reaches
   (F4).
5. The API fits. An adapter from `Peers::fetch` to Detent's `Fetch(name, Fetched)` is a loop over
   the peers: an error or `nullopt` moves to the next peer, and the bytes move through without a
   copy. `History::find` already has the `FetchReader` shape. History files take one path (this
   lane), and the snapshot channel keeps snapshots, as D91 rules.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A fetch that times out keeps its slot until its reply ends, and nothing else ever frees it. Once every slot to a peer is held by a failed fetch, `fetch` refuses at once and never writes to the lane. Nothing checks an idle bulk lane: it has no keepalive, and its first-byte read has no deadline. So a peer that will never answer leaves fetches to it refused for as long as the lane stays open. That peer may have lost power behind a half-open TCP connection, or it may have a bug. That lane is permanent: Detent's history recovery loses that peer as a source. | Probe 261113-021451-c6bcfc (`review-stuck`); peers.h:215; session.h:153-155 | Keep the "slow is not broken" ruling for one deadline, then recycle the lane. Close the bulk lane when a fetch finds every slot held by fetches that failed at least `fetch_deadline` ago, or give each failed fetch a second deadline. Reconnecting resets the counts at both ends. Test: a variant of `review-stuck` where the peer stays hung. A later fetch completes with `connection_aborted` or `not_connected` instead of `resource_unavailable_try_again` forever, and a fetch after the peer resumes returns the bytes. |
| F2 | no | gap | A mismatch in `fetches` is documented, not detected. When the requester allows more than the server, the server closes the bulk lane on the excess request. That aborts every fetch on the lane (6 of 6 in the probe) and any snapshot transfer in flight, every time. The only signal is `rejected` +1 on the server. | Probe 261113-021451-c6bcfc (`review-mismatch`); peers.h:357 | Options: (a) put `fetches` in the hello or the profile fingerprint, so a mismatch fails the handshake loudly; (b) have the server answer the excess with `Failed` and not close the lane; (c) keep it, with Detent pinning one value in code (it uses the default today). |
| F3 | no | gap | The serving voter calls the reader synchronously on the owner executor, once per reply, for the whole file. In Detent, `Directory::find` calls `read_all` on a disk file, so the read time adds to consensus latency on the voter that serves, which may be the leader. A2's reader is an in-memory map, so A2 does not measure this. The doc's "a fetch holds [the executor] for one frame's encryption at a time" is inaccurate. | peers.h:417; docs/mechanisms/peer-fetch.md:65; Detent namespace-s5-3 history.cc:219-224 | State in fetch.h and the doc that the reader's run time lands on the owner executor. namespace-s5-3 measures append round trips while serving its largest history file from disk, or serves the file from memory. |
| F4 | no | gap | The A2 assertion `slowest_ns <= 64'000'000` is a histogram bucket edge. Release lands in the <= 16 ms bucket, but TSan lands in the (16, 64] bucket in 10 of 10 runs, idle and loaded. The next bucket fails, and the histogram cannot say how close each run came. On slower CI runners, `ci:linux-tsan` may flake on timing while consensus is fine. | Probe 261113-021453-acea31; peers_test.cc:1132 | Record the exact slowest round trip, not the bucket. Assert it below the election minimum (100 ms), or below 64 ms in Release only. |
| F5 | no | gap | The requester allocates replies outside the byte budgets. It reserves the claimed `total`, up to `fetch_bytes`, on the first Data frame and assembles the reply outside `incoming_bytes`. One byte from a pinned peer therefore costs up to 64 MiB, for ids we asked. The worst case is peers × `fetches` × `fetch_bytes` (4 × 4 × 64 MiB = 1 GiB), and the doc does not state it. | peers.h:373; docs/mechanisms/peer-fetch.md "Budgets and consensus" | State the bound in the doc. Optionally, grow the buffer as chunks arrive instead of reserving the claimed total. |
| F6 | no | gap | The name is untrusted input from a peer, and the reader gets it unchecked. The doc and fetch.h do not say so. Detent's reader opens it under the history IO. | fetch.h:15; peers.h:417 | Say so in fetch.h. namespace-s5-3 confines names to its history file names (security lens). |
| F7 | no | gap | No deterministic test covers a local `stop()` with fetches pending, which should give `connection_aborted` and then `drained()`. No test covers a deadline handler that is already queued when the final frame arrives. Both are correct by reading (peers.h:138-139, 230), and the peer-close path is tested and mutant-killed. | peers_test.cc F3; mutant 261113-020124-cd1754 | Add a `stop()`-with-pending-fetches case to F3. |
| F8 | no | nit | Pre-existing, outside this quest: a stale, idle bulk lane also makes `run()` reject a restarted peer's redial as a duplicate lane. So the half-open problem behind F1 also blocks snapshots from that peer. | peers.h `run`: `find(remote.node, remote.lane)` | Follow-up quest on bulk-lane liveness (TCP keepalive or a bulk ping); not here. |

## Not run

- `ci:linux-tsan` and the Linux matrix: CI's job, and they need the branch pushed.
- TSan on Linux: I ran macOS llvm only.
- A real half-open TCP lane: I reasoned it from session.h:153 and the absence of a keepalive. The
  probe shows a peer that hangs, not a host that vanished.
- Detent's adapter and its reader: they belong to namespace-s5-3.
- A fuzz run of the full `received()` state machine: the fuzz job covers the decoder only.
