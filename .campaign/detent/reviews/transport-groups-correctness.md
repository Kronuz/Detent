# transport-groups review: correctness

Commit: consensus:81339c26c88f7634b79da101121c8186100c0484
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The design holds: routing, the accept hand-off, control-before-data and per-group credit are
correct in the code, and the hand-off and restart paths stay clean under TSan in my probes. It
does not land yet, for three reasons:

1. CI at this commit is red on every platform, and the cause is this quest's code.
2. Two macOS CI runs hang with no output, and nothing explains it yet.
3. No suite test proves that credit comes back. Both return paths can be removed and every case
   still passes.

## Evidence I relied on

- Suite 261117-050041-9d19b2 and the cases at 81339c2: `threads` 032a41, `credit` 6bc00a,
  `reader` ed123d, `upload` 41c3d7 and `driver` b75526. The TSan job a4b2ed covers
  `groups_threads`, `groups_credit`, `groups_driver`, `connection` and `links` (macOS, clang). I
  read the logs and accept their values.
- Mutants, each run with `--fails-without` and each failing as it must: bulk-ahead-of-control
  1bc50d (gap 4,005 ms), no-credit bf0e3f, reader-loop-delivers 527d2f and no-hand-off d84fdc.
  They show that `upload`, `credit` and `threads` test what they claim.
- GitHub Actions CI run 44089495483 at 81339c2 (`gh run view 44089495483`), which concluded
  failure. Its logs are saved under `.scratch/review-transport-groups-correctness/ci/` in the
  home checkout.
  - All five Linux jobs (linux-tsan, linux-asan-ubsan, linux-x86_64-clang, linux-x86_64-gcc,
    linux-arm64-gcc) fail `consensus_transport_connection` at `connection_test.cc:614`
    (`read_slowly`). That is the same assertion in all five; every other test passes, the
    groups tests included.
  - `consensus_transport_groups_reader` times out at 120 s on macos-arm64 and macos-x86_64.
    `consensus_transport_groups_upload` also times out on macos-x86_64.
  - Main at the merge base, `consensus:677c864`, is green on the same workflow (run
    44083236604).

## What I checked myself

The scripts are under `.scratch/review-transport-groups-correctness/` in the home checkout.
`probe_commit.py REPO REF [MUTANT...]` writes a commit on no branch that adds
`review_probe.cc` (built beside `groups_test.cc`, ctest `consensus_transport_review_{credit,handoff}`)
plus any named mutants. `hang.py` runs cases two at a time under a 60 s watchdog and samples
the stacks of a hung run.

- **Probe at consensus:272c9d2 (81339c2 plus the probe).** Release cbf513 and TSan 3b7b42 both
  pass.
  - `credit` uses a 64 KiB window. It shows a frame arriving after 400 refused frames, and
    after the receiver restarts with its window full.
  - `handoff` runs 40 rounds. Each round stops the acceptor, or the dialer and then the
    acceptor, at 0 to 10 ms, across the TLS handshake and the move to the owner loop. Node 1
    is placed on loop 1 of 4. 20 to 23 rounds established a link before the stop. Every loop
    drained (`incoming_` included), and TSan reported nothing.
- **Mutant no-credit-on-refuse** (credit only for frames that were taken, at db89a6c). The
  plain `threads`, `credit` and `reader` cases pass (d9e129): the mutant survives. The probe
  kills it (9ca635: wedged after 3 refusals).
- **Mutant no-reset-on-end** (outstanding credit kept across a connection's end and the next
  connection's start, at 1b6d156). The plain `threads`, `credit` and `driver` cases pass
  (c7204f): it survives. The probe kills it (32976c: no frame after the restart).
- **Mutant feed-unbounded** (the 64 KiB feed bound removed, at 1b8db1d). `upload` passes with a
  longest gap of 819 ms (4cb9d4): it survives.
- **Mutant no-lowat** (no `TCP_NOTSENT_LOWAT`, at a0a355b). `upload` passes with a gap of 233 ms
  (109ee5): it survives on macOS loopback.
- **A2 margin.** `reader` and `upload` ran two at a time for 8 rounds (2a0b71). There were no
  hangs. The `upload` gap was 209 to 248 ms and the `reader` gap 102 to 111 ms. Job b66a71
  (upload x5) failed on a missing TLS fixture, which was my script's fault; 2a0b71 replaces it.
- **Code reading.**
  - Placement and the frame route (`host_transport.h:84-89`, `:337`).
  - The hand-off: `links.h:185-203` and `connection.h:75-79`. Nothing waits on the socket
    when it moves. The hellos and the tie-break run on the owner loop, and no frame flows
    before `start`.
  - Feed order (`:249-281`).
  - Credit accounting: `:366-394`, plus the generation checks.
  - The Asio rule. `task::spawn` posts and never runs inline (task.hh:933-942).
    `late_deadline::wake` posts the resume. Every cross-loop hop uses `asio::post`.
  - Waits. The only new waits are the `late_deadline`s in `accept` and `adopt`
    (`links.h:175`, `:209`). `host_transport.h` waits on nothing; redial compares against
    `now()`.

## Answers to the lead's questions

1. **Routing.** A frame cannot reach the wrong loop: the receiver computes the group's loop from
   the frame's group id with its own L, and the `reader-loop-delivers` mutant is killed. An
   accept moves before its hellos, and the host transport sees the link only at `start` on the
   owner loop, so nothing is delivered on loop 0 and nothing is lost in the move. The hand-off
   probe also covers a stop during the move. The 16-byte route is not stored, which the journal
   rules on; see N4.
2. **Batching and credit.**
   - A full group's credit only gates its own data (`next(lane, true)`). Control frames and
     credit go first, so no group stalls another's control frames.
   - Credit on data frames only is sound for honest peers: Raft's control traffic is small
     messages at a bounded rate. A misbehaving authenticated peer can exhaust memory with
     control frames, or by ignoring credit, because the receiver enforces neither. That
     belongs to the security and admission quests (N2).
   - Credit comes back exactly once in the code: per batch, and dropped by the generation
     check when its connection has ended. The suite does not prove it (F3).
   - One edge leaks frames into the next read's batch (N1).
3. **A2 margin.** On this host the gap is 195 to 248 ms against 1,000 ms, about 4x. On macOS
   x86_64 CI, `upload` hung (F2), so there is no number for that host.
   - The `bulk-ahead-of-control` mutant tests the right thing: priority order. The limits
     behind the margin are not load-bearing in the test, though. Without the 64 KiB feed the
     gap is 819 ms and the test still passes; without `TCP_NOTSENT_LOWAT` it is 233 ms (N3).
   - The gap is set mostly by kernel socket buffers divided by the 40 MB/s throttle, so a
     slower link scales it.
4. **Concurrency.** The `connection.h` and `links.h` hooks are safe across loops:
   - `rehome` runs on the accepting loop. It hands over with a post, which gives the
     happens-before, and `incoming_` is atomic.
   - `on_read` and `on_drained` run on the connection's loop, and a wake posts rather than
     re-entering.
   - TSan a4b2ed and my probe 3b7b42 cover the hand-off, a stop during the hand-off, and a peer
     restart mid-window.
   - Closing a group with frames queued has no API here. Queues stay bounded (64 control frames
     and 8 MiB per group per connection), and the receiver's `Deliver` refuses those frames with
     credit returned.
   - The macOS CI hang is unexplained (F2).
5. **A3.** Yes, `driver` passes: the leader restarts (term 2) and both groups commit again on
   all three nodes. One in-memory journal per loop does not hide a transport problem. The one
   real-journal property it skips is that the journal host's and the transport's placement
   agree; both call `host_detail::loop(slot(g), L)`, but no test asserts it (N5).
6. **The Asio rule and `late_deadline`.** Both hold (see Code reading above).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | CI is red on all five Linux jobs: `connection_test.cc:614` `read_slowly` asserts. The only change at this commit that the connection test sees is `TCP_NOTSENT_LOWAT` (no hooks are set there), and main at the merge base is green. `no-lowat` also shows the option buys nothing measurable in `upload` on macOS (233 ms against 195 ms). | CI run 44089495483 against run 44083236604; `connection.h:48-51`; job 109ee5 | Either drop `TCP_NOTSENT_LOWAT`, or keep it with a measured case for it and make `read_slowly` hold on Linux. Confirm by running CI on mutant commit `consensus:a0a355b` (no-lowat), or on the fix. The fix passes when all five Linux jobs are green. |
| F2 | yes | gap | `groups_reader` hangs (120 s ctest timeout, no output) on macos-arm64 and macos-x86_64, and `groups_upload` on macos-x86_64. Locally, 16 runs two at a time did not hang, and nothing found by reading the code explains it. A hang in a test that stops and drains hosts can be a drain or shutdown defect, so it is consequential until explained. | CI run 44089495483 (jobs 134061282962, 134061282942); job 2a0b71 | Make the case observable on CI: line-buffer stdout, and dump the stacks on a watchdog, as `hang.py` does with `sample`. Find the cause and fix it. The fix passes when both macOS jobs are green. |
| F3 | yes | gap | Credit return is not proven. Credit for refused frames (A1's own scenario, "a group with full admission") and the outstanding reset at a connection's end and the next one's start can each be removed with every suite case passing. Either leak permanently wedges the group's data once a window's worth is lost. `reader` cannot catch the first: 300 KiB of refused appends never fill the 4 MiB window. | Survivors d9e129 and c7204f; killed by probe 9ca635 and 32976c (`review_probe.cc` `credit`) | Add a suite case like the probe: a small window, refusals past it, then a receiver restart with its window full, and a frame must arrive after each. Run `no-credit-on-refuse` and `no-reset-on-end` as `--fails-without` mutants. |
| N1 | no | defect | A frame length over `maximum_frame` makes `Connection::deliver` throw before `on_read`. The frames this read had already handed to `arrived` stay in `loops_[k].in`. The next read on that loop, by any connection, posts them stamped with its own node, lane and generation, so their credit goes to the wrong connection, and they may wait indefinitely. Only a faulty peer triggers it. | `connection.h:229-230`, `:246-248`; `host_transport.h:337`, `:348-351` | Call `on_read` on the reader's failure path too, or have `deliver` fail the connection and return instead of throwing. |
| N2 | no | gap | The receiver enforces neither the window nor any bound on control frames: an authenticated peer that ignores credit, or floods control frames toward a stalled loop, grows that loop's posted batches without limit. The hellos phase is also no longer inside the pre-authentication pool (`authenticated()` now runs right after TLS). | `host_transport.h:312-338`; `links.h:179` | Belongs to transport-identity, transport-admission and the security review; name it there. |
| N3 | no | gap | Limits behind A2's margin are not load-bearing in `upload`. Without the 64 KiB feed bound the gap is 819 ms and passes, 18% under the timeout; without `TCP_NOTSENT_LOWAT` it is unchanged. | Jobs 4cb9d4 and 109ee5 | Assert the gap well under the timeout (for example under 500 ms), or add a case that kills `feed-unbounded`. |
| N4 | no | nit | The quest's outcome names "a 16-byte route"; the code stores none (placement is arithmetic, and frames carry a 25-byte header). The journal rules on this deliberately; the lead should accept or reject the ruling. | Journal, Rulings; `host_transport.h:25` | The lead's call. |
| N5 | no | nit | Nothing asserts that the journal host and the transport place a group on the same loop. Both use `host_detail::loop(slot(g), L)`, but A3's per-loop journals never exercise it. Bulk is never dialed by any test. | `groups_test.cc` `Node` | A one-line assertion against `j::Host`'s placement when Driver hosts adopt the transport. |
| N6 | no | nit | `consensus_transport_peers` failed on macos-x86_64 (`peers_test.cc:1196`). `peers.h` does not include `connection.h` or `links.h` and is unchanged, so this is not attributable to this quest; likely the known macOS x86_64 flake. | CI job 134061282942 | None here. |

## Not run

- A Linux run of the no-lowat mutant (CI's, and it needs a push). The attribution in F1 rests
  on the diff and on main being green.
- The macOS CI hang's root cause (F2); it did not reproduce on this host.
- `reader` and `upload` under TSan, and any sanitizer other than TSan locally.
- A crash, as opposed to an orderly stop, of a node holding the real journal.
- The Bulk lane, and a group close.
