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

## Round 2

Commit: consensus:c0501230b7d3cfe81eb2c0feaf9c548f345badc4
Verdict: CHANGES
Date: 2026-11-17

The diff `81339c2..c050123` closes F1 on Linux and closes F2 and F3. It does not land yet, for
two reasons:

1. CI is red on macos-x86_64, and the failing case is the fix's own `coarse_writer`.
2. No suite test shows that the stuck-write deadline still ends a connection whose peer never
   reads. An off-by-one in the new comparison keeps such a peer alive indefinitely, and the
   suite still passes.

### Evidence I relied on

- GitHub Actions CI run 44103387834 at c050123 concluded failure. Eight jobs passed:
  linux-tsan, linux-asan-ubsan, linux-x86_64-clang, linux-x86_64-gcc, linux-arm64-gcc,
  freebsd-x86_64 (where `connection` runs `coarse_writer` through the `FIONWRITE` branch) and
  macos-arm64. macos-x86_64 failed one test, `consensus_transport_connection`:
  `coarse_writer: 9 of 48 frames read in 1150 ms ... dialer 2, acceptor 2` at
  `connection_test.cc:624`. Its `groups_reader`, `groups_upload` and `refill` passed. The logs
  are in `.scratch/review-transport-groups-correctness/ci-r2/`.
- The contributor's jobs at c050123, whose logs I read: `connection` a84d63 (`coarse_writer`
  48 of 48 in 3,180 ms); `refill` d3fe59 (100 refused, then a marker; a marker after the
  restart). The mutants failed as they must: `stuck-ignores-drain` 5deb78 (6 of 48, dialer
  failed at 399 ms), `no-credit-on-refuse` bc6eb9 (no marker after the refusals) and
  `no-reset-on-end` 572024 (no marker after the restart). Mutant diffs checked against
  c050123. All ids are 261117-.

### What I checked myself

The script is `probe_stuck_commit.py REPO REF [MUTANT...]`, which adds `review_stuck.cc`
(ctest `consensus_transport_review_stuck_{nonreader,coarse}`). It is under
`.scratch/review-transport-groups-correctness/` in the home checkout. All job ids are 261117-.

- **`nonreader`.** A loopback relay stops forwarding toward the peer after the hellos. The peer
  keeps sending keepalives with a 20 s `dead_after`. The writer has a 300 ms `dead_after` and
  32 MiB queued.
  - At c050123 (probe `ee81ac5`, job 063603-e51b37), the writer ends `failed` after 336 ms
    while the peer is still open. A non-reading peer is not kept alive.
  - Mutant `stuck-equal-drains` (`>=` becomes `>`, `8ee1289`, job 063607-b08380): the writer
    is still open at 8 s, with 157 keepalives received.
  - Mutant `stuck-never-fails` (`af1b491`, job 063610-8cdc75): still open at 8 s, and the
    suite's `connection` passes with all 15 cases (F5).
- **`coarse` traced** (`99865c8`, job 063626-ba250e). `coarse_writer` runs ten times in each of
  two processes under `taskpolicy -b`, and each stuck expiry is printed. Both processes fail
  the way CI did: `4 of 48 frames read in 861 ms`, dialer failed, with process stalls of 91 to
  96 ms, under the 100 ms retry threshold. The trace reads `queued 1050058 now 1039864` (a
  drain, so it continues), then `queued 1039864 now 1056270` (it fails). In the failing
  window the queue grew by exactly one TLS record (16,406 bytes) after it was sampled, and
  drained nothing in 300 ms (F4).
- **Code reading** (`connection.h` at c050123).
  - **Per platform.** `SIOCOUTQ` (Linux, `linux/sockios.h`) is `write_seq - snd_una`.
    `SO_NWRITE` (macOS) is `so_snd.sb_cc`. `FIONWRITE` (FreeBSD, through `sys/ioctl.h`) is the
    used part of `so_snd`. All three count bytes written and not yet acknowledged, so each
    drains only on the peer's ACKs. Where none is defined, it returns 0 and `0 >= 0` throws,
    which is the old behavior.
  - **ACK without reading.** A peer whose kernel ACKs but whose application never reads stops
    the drain once its receive window closes. Linux and macOS persist probes carry no
    accepted new data. The extension is therefore bounded by the peer's receive buffer plus
    one `dead_after`, and `nonreader` shows it.
  - **Cancellation.** Expiry cancels only the writer's `async_wait`, because `await_asio` uses
    a per-operation slot. The reader's wait survives the retry. A retried `SSL_write` reuses
    the same buffer, which `SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER` already allows.
- **F2.** Both `sleep_until` sites in the repo (`groups_test.cc:487` and `:546`) now use
  `std::int64_t` ticks. The watchdog, `step()`, `PR_SET_PTRACER` and the diagnostics are all
  in `groups_test.cc`. No product file changed except `connection.h`.

### Answers to the lead's questions

1. **F1.**
   - **Linux and FreeBSD.** Correct: every Linux job and freebsd-x86_64 are green.
   - **macOS.** The mechanism works on macOS, because the trace shows a drain extending the
     deadline. The case does not hold on slow hosts, though (F4).
   - **A dead or non-reading peer.** It is not kept alive. A peer that ACKs but never reads is
     bounded by its receive buffer: 336 ms in `nonreader`. Nothing in the suite guards this
     (F5).
   - **The comparison.** Two races:
     - `queued` is sampled before `flush` writes. Bytes the writer itself adds during the
       window count against the drain, which the trace shows (+16,406). This can only end a
       live connection early, never extend a dead one.
     - A failed `ioctl` at expiry returns 0, which reads as a drain. That grants at most one
       extra `dead_after` (N9).
2. **F2.** Fixed everywhere such a tick exists. Both macOS CI jobs pass `reader` and `upload`.
   The watchdog and the diagnostics are test-only and cost the product nothing. The product's
   one new cost is the `unacknowledged()` syscall each time the writer arms (N8).
3. **F3.** Yes. `refill` fails under `no-credit-on-refuse` (bc6eb9) and under
   `no-reset-on-end` (572024), and passes at c050123 on every CI platform.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Closed on Linux and FreeBSD: all five Linux jobs and freebsd-x86_64 are green. `TCP_NOTSENT_LOWAT` is gone. | CI run 44103387834 | Done. |
| F2 | no | gap | Closed: the cause was the unsigned pacing tick. Both macOS jobs pass `reader` and `upload`. The watchdog is test-only. | CI run 44103387834; `groups_test.cc:487`, `:546` | Done. |
| F3 | no | gap | Closed: `refill` kills both credit-return mutants. | Jobs d3fe59, bc6eb9, 572024 | Done. |
| F4 | yes | defect | macos-x86_64 CI fails `coarse_writer` (9 of 48 in 1,150 ms), and the failure reproduces locally under background QoS (4 of 48 in 861 ms, both processes). The case assumes the reader drains the queue in every 300 ms window. On a slow host it does not (one window drained 0 bytes), and the process-stall retry does not see a slow reader thread (91 to 96 ms against its 100 ms threshold). The rule also samples `queued` before `flush`'s own writes, so the writer's records (+16,406 in that window) count against the drain. | CI job 134109191241; job 063626-ba250e trace; `connection.h:292-298` | Sample the queue after the flush's last write, right before each wait (or keep the lowest value seen), so that only the peer's drain counts. Give `coarse_writer` margin, for example a longer `dead_after` with a buffer that takes longer than that to wake, and print its expiries. It passes when macos-x86_64 is green and ba250e's script passes. |
| F5 | yes | gap | Nothing in the suite shows the stuck-write deadline still ends a connection whose peer keeps talking but never reads. That is the safety half of the F1 fix. `stuck-never-fails`, and the off-by-one `stuck-equal-drains`, each keep such a peer open indefinitely, and `connection` still passes. | Jobs 063603-e51b37 (passes at c050123: failed at 336 ms), 063607-b08380 and 063610-8cdc75 (open at 8 s) | Add `review_stuck.cc`'s `nonreader` to `connection_test` and run `stuck-never-fails` as a `--fails-without` mutant. |
| N7 | no | defect | The close path keeps the old rule. `SSL_shutdown` waits for writability under one `dead_after` with no drain check. On Linux, a close_notify behind a full send buffer to a slow reader waits for the one-third wake and can end `failed`, which is F1's cause on a second path. Found by reading the code; not reproduced. | `connection.h:314-318` | Use one stuck rule for both waits: a helper that `flush` and the close path share. |
| N8 | no | nit | `unacknowledged()` costs one syscall each time the writer arms, which is once per batch, even when the write never waits. | `connection.h:292` | Sampling right before a wait (F4) also removes this cost. |
| N9 | no | nit | A failed `ioctl` or `getsockopt` returns 0, and at expiry that reads as a drain. That grants at most one extra `dead_after`. | `connection.h:226-234` | Treat a failure at expiry as no drain. |

Round 1's N1 to N5 stand unchanged; N3 is not done, as the journal says. N6 did not recur:
`peers` passed on macos-x86_64 in run 44103387834.

### Not run

- `nonreader` on Linux or FreeBSD (it needs CI), and `coarse_writer` with the F4 fix.
- A forced close_notify behind a full buffer (N7).
- The `refill` mutants on CI platforms other than this macOS host.

## Round 3

Commit: consensus:e4e394b06482b4e8a02f6aae5fbc69a12e40bbeb
Verdict: APPROVE
Date: 2026-11-17

The diff `c050123..e4e394b` closes F4 and F5, and N7 to N9. CI is green on every platform,
macos-x86_64 included, and no finding blocks.

### Evidence I relied on

- GitHub Actions CI run 44116972577 at e4e394b: concluded success, with all nine jobs green (five Linux, freebsd-x86_64, macos-arm64, macos-x86_64 and plan; the headers matrix was skipped). On macos-x86_64,
  `consensus_transport_connection` passed in 20.0 s, and `groups_reader`, `groups_upload` and
  `refill` passed. The logs are in `.scratch/review-transport-groups-correctness/ci-r3/`.
- The contributor's jobs at e4e394b, whose logs I read (all ids 261117-):
  - Suite 073408-7f66d4 and TSan 073414-ce6391.
  - `connection` 073411-513aec: `coarse_writer` read 48 of 48 in 6,421 ms with 1 drained
    expiry and a longest hold of 265 ms. `nonreader` failed at 409 ms.
  - Background QoS 073418-e06e0e, strict, two processes, 10 rounds each: every `coarse_writer`
    passed on its first try with 2 drained expiries and holds of 278 to 696 ms. Every
    `nonreader` ended `failed`.
  - Mutants, each run on `connection` and each failing as it must:
    - `stuck-never-fails` 073422-360ef2 and `stuck-equal-drains` 073427-26d576 left the
      `nonreader` writer open at 10 s with 32 drained expiries (`connection_test.cc:806`).
      Their commits differ from e4e394b by the one intended line each.
    - `stuck-samples-before-flush` 073431-413a4a (16 of 48) and `stuck-ignores-drain`
      073436-adb7b0 (17 of 48) ended `coarse_writer` at its first expiry. Their holds were
      264 to 267 ms, so the held-queue retry did not hide them.
    - `stuck-spans-flush` 073441-916a42 and `no-close-notify` 073445-25fcd7 also failed.

### What I checked myself

The script is `probe_r3_commit.py REPO REF`, under `.scratch/review-transport-groups-correctness/`
in the home checkout. It writes a commit on no branch that adds the case `slow_then_stops` to
`connection_test.cc`. It also checks that every search string in `mutant_commit.py` occurs
exactly once at REF. All job ids are 261117-.

- **`slow_then_stops`** (probe commit `948d5e5`, job 074439-dd5c1e). The writer is coarse:
  1.5 MiB send buffer and low-water mark, 2 s `dead_after`. It writes to a relay that forwards
  64 KiB every 125 ms for 6 s and then stops forwarding for good. The peer keeps sending
  keepalives.
  - 3 of 3 runs stayed open through the slow phase with 2 drained expiries.
  - Each run then ended `failed` 2,008 to 2,016 ms after the stop. That is one `dead_after`,
    with no extra window.
  - In the same job, `nonreader` failed at 405 ms.
- **Mutant search strings.** All 34 occur exactly once at e4e394b. The three that `ff0d58b`
  repaired are each killed by an assertion, not by a build failure:
  - `no-extension` 074451-9cb882: `deadline_test.cc:68`, `stop_extends`.
  - `twice` 074455-8ee11e: `deadline_test.cc:103`, `once`.
  - `abandon-keeps-dial` 074458-95a996: `links_test.cc:340`, which reports "the given-up
    connection still held".
- **Code reading** (`connection.h` at e4e394b).
  - `want_written` samples the queue after the flush's own writes, right before each wait. The
    send queue grows only on this end's writes and shrinks only on the peer's ACKs (or a reset,
    which fails the next write), and the comparison is strict.
  - At expiry the caller retries its write, so the next wait samples the queue again. A peer
    whose kernel stops accepting therefore ends the connection at the first window with no ACK.
  - The extension is bounded by what the peer's kernel still buffers, which the probe shows.
  - `flush` without a deadline and the `close_notify` loop both go through `want_written`, so
    there is one stuck rule (N7). The hellos keep the handshake deadline.
  - `unacknowledged()` runs only before a wait and at its expiry (N8). A failed reading is
    `nullopt`, which never counts as a drain (N9).

### Answers to the lead's questions

1. **F4.**
   - **No window.** A peer that never drains does not survive a deadline. The only bytes that
     can shrink the sample are the peer's ACKs. Once its buffer fills, the next wait ends the
     connection: `slow_then_stops` ends at one `dead_after` after the stop, and `nonreader`
     at 405 to 547 ms.
   - **The margin hides nothing.** The margin is in how often the queue drains, about once
     every 125 ms against a 2 s window, not in whether the rule is reached. Each wait runs
     3 s or more, so where the low-water mark takes (macOS and FreeBSD) every pass needs a
     drained expiry, and the case asserts one. Both mutants that break the rule fail at the
     first expiry.
   - **Linux.** The case cannot assert that it reaches the rule there (N10).
2. **F5.** Yes. `nonreader` ends a peer that never reads in 405 to 547 ms, here and under
   background QoS. Both ported mutants fail it (360ef2, 26d576), and the suite now carries it.
3. **N7 to N9 and `ff0d58b`.** All are sound.
   - No case forces `close_notify` behind a full buffer. N7 rests on the code being shared.
   - All 34 mutant search strings resolve exactly once, and the three repaired mutants are
     killed by their cases.
4. **CI.** Run 44116972577: concluded success, with all nine jobs green (five Linux, freebsd-x86_64, macos-arm64, macos-x86_64 and plan; the headers matrix was skipped).

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F4 | no | defect | Closed. Each write wait samples the queue after its own writes and has its own `dead_after`. macos-x86_64 CI is green, and background QoS passes 20 of 20 on the first try. | CI run 44116972577; jobs 513aec, e06e0e, 413a4a, adb7b0; probe dd5c1e | Done. |
| F5 | no | gap | Closed. `nonreader` is in the suite and kills `stuck-never-fails` and `stuck-equal-drains`. | Jobs 513aec, 360ef2, 26d576 | Done. |
| N7 | no | defect | Closed in code: `close_notify` uses `want_written`. No test forces that path. | `connection.h:333` | Done. |
| N8 | no | nit | Closed: the syscall runs only before a wait and at its expiry. | `connection.h:228-229` | Done. |
| N9 | no | nit | Closed: a failed reading is `nullopt` and never counts as a drain. | `connection.h:241-262` | Done. |
| N10 | no | gap | On Linux, `SO_SNDLOWAT` is refused, so `coarse_wakes` is false and `coarse_writer` asserts nothing about reaching the drain rule. CI's ctest prints no case lines for a passing test, so no record shows whether any Linux run reaches a drained expiry. The `stuck-*` mutants ran only on macOS. | `connection_test.cc:589-599`, `:687`; CI run 44116972577 logs | Optional: have CI print `connection`'s lines, for example with `-V` for that test. Or run one `stuck-ignores-drain` mutant on a Linux CI runner. |
| N11 | no | nit | Lines the diff adds to `CHANGELOG.md` (line 20, 129 columns) and `host-connection.md` (line 17, 116 columns) are not rewrapped. | Those lines | Rewrap. |

Round 1's N1 to N5 stand as they were.

### Not run

- `slow_then_stops` on Linux or FreeBSD, and under background QoS.
- A forced `close_notify` behind a full buffer (N7).
- A Linux run of any `stuck-*` mutant.
