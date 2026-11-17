# transport-groups: journal

State: fix round 2 at consensus `ff0d58b`; waiting on jobs (ids under "Fix round 2 jobs")
Quest: .campaign/detent/quests/transport-groups.md (the lead's; never edit it)

## Doing

Fix round 2 at consensus `ff0d58b` (on `c050123`): `3ba44aa` gives each write wait its own
stuck deadline, sampled after its writes (F4, N7, N8, N9), widens `coarse_writer`'s margins and
adds `nonreader` (F5); `ff0d58b` points three stale mutants at the current code.

## Next

1. Read the round 2 jobs; the lead pushes for CI (macos-x86_64 `coarse_writer`, every
   platform's `nonreader`).

## Blocked on

Nothing.

## Fix round 2 (review round 2, same file)

- F4 (`3ba44aa`). Product: the window sampled `unacknowledged()` before flush's writes, so a
  record written in the window (OpenSSL writes a pending record, then returns WANT_WRITE on the
  next) counted against the drain. Now every writer wait (`want_written`) has its own
  `dead_after`, the queue sampled right before it, after this end's writes; at expiry a drained
  queue returns to the caller, which retries its write. `flush` lost its `progress` early
  return (a wait is the window now). Test: the reviewer's failing window drained 0 bytes in
  300 ms, which no sampling point saves, so the margin is what fixes that run:
  `coarse_writer` is 40 x 64 KiB at 125 ms per frame against a 1 s `dead_after`; the 1 MiB
  queue empties (the low-water mark's wake) in ~2 s, the reader drains ~8 frames per window
  (>= 4 with 100 ms timer coalescing); where the low-water mark takes (macOS, FreeBSD) it must
  see a drained expiry (`Connection::drained_expiries()`), so it cannot pass without reaching
  the rule; the run wait is 60 s and the `connection` ctest timeout 120 s. Local: 40 of 40 in
  5,331 ms, 3 drained expiries. Mutant `stuck-samples-before-flush` samples when the flush
  begins (the old point, before its writes).
- F5 (`3ba44aa`). `nonreader` from the reviewer's probe: a relay stops forwarding to the peer
  after the hellos; the peer keeps sending keepalives (`dead_after` 60 s); the writer
  (`dead_after` 300 ms, 32 MiB queued) must end `failed` within 5 s (wait 10 s). Local: failed at
  409 ms. Mutants `stuck-never-fails` and `stuck-equal-drains` are the reviewer's, ported to the
  new code (`|| !drained(before))` -> `|| false)`; `*now < *before` -> `<=`).
- N7 (`3ba44aa`): close_notify's waits go through `want_written`, one rule for both. No case
  forces close_notify behind a full buffer (it needs the frames to fill the buffer exactly), so
  that path is covered by sharing the code, not by a test of its own.
- N8, N9 (`3ba44aa`): the syscall runs only before a wait and at its expiry; a failed reading
  is `nullopt` and never a drain.
- `stuck-spans-flush` follows the new code: one deadline for all of a flush's waits.

## Fix round 2 jobs

At consensus `ff0d58b`, all ids 261117-: suite (`campaign verify`) 071128-07f342; `connection`
071327-13783b; TSan (threads, credit, driver, refill, connection, links) 071331-293bdd;
background QoS (`coarse_writer` then `nonreader`, 10 times in each of two processes under
`taskpolicy -b`, strict) 071309-6d3791. Mutants (`--fails-without`): on `connection`
`stuck-never-fails` 071200-899f1b, `stuck-equal-drains` 071204-847ba9,
`stuck-samples-before-flush` 071208-83ccf6, `stuck-ignores-drain` 071212-a2710f,
`stuck-spans-flush` 071217-ff4aa1, `no-close-notify` 071221-7964d2; `no-credit-on-refuse`
071226-56996e and `no-reset-on-end` 071231-42c1a9 on `refill`; `bulk-ahead-of-control`
071236-d8d3bd on `upload`; `no-credit` 071240-908cac on `credit`; `reader-loop-delivers`
071245-458051 and `no-hand-off` 071249-042e02 on `threads`. Void: 071137-07234a and
071140-3aeac0 (my quoting ran them at the home checkout's commit, not consensus).
Results: pending.

## Fix round 1 (review .campaign/detent/reviews/transport-groups-correctness.md)

- F1 (`4daed86`, `c050123`). Cause: the stuck-write deadline restarted only at a write that
  made progress, so it timed how often the kernel woke the writer, not whether the peer read.
  Linux wakes a full socket only once a third of its send buffer is free (up to ~1.3 MiB of a
  4 MiB autotuned buffer), and with `TCP_NOTSENT_LOWAT` a write queues only under the mark
  (`tcp_sendmsg` checks it, not only `EPOLLOUT`), so progress comes only as the window opens.
  At a slow reader's pace either can exceed `dead_after`: `read_slowly` flaked on main
  (linux-tsan at rpc-lane-codec `8db3ce3`, the lead's msg) and failed on all five Linux jobs
  with lowat (run 44089495483); the lead's no-lowat mutant `a0a355b` (branch
  probe/transport-groups-no-lowat; the push trigger skips probe/*, so I dispatched run
  44097748266) passed every Linux job, `groups_upload` included. Fix `c050123`: at expiry, a
  socket whose send queue (`SIOCOUTQ`, `SO_NWRITE`, `FIONWRITE`: written, unacknowledged)
  drained since arming gets the deadline again. New case `coarse_writer` (send buffer 1 MiB
  with a 1 MiB low-water mark where settable, reader buffer 64 KiB, 64 KiB per 60 ms against
  300 ms); mutant `stuck-ignores-drain`. `connection_test` stdout is line-buffered so a
  failing case's line reaches the CI log. Lowat dropped (`4daed86`): no measured A2 gain
  (macOS 233 ms without, 195 ms with, reviewer 109ee5).
- F2 (`6b2c6fa`). Cause: `reader` and `upload` paced with `sleep_until(start + (tick + 1) * P)`,
  `tick` a `std::uint64_t`. That makes the time point's rep unsigned, and libc++'s
  `sleep_until` for `steady_clock` is `sleep_for(t - now())` (SDK `__thread/this_thread.h:65`),
  so a slot already past (one 10 ms tick overrun on a loaded runner) underflows to centuries.
  libstdc++ loops on `now() < t`, which is why Linux never hung. The no-lowat run's
  macos-x86_64 still hangs both cases, so lowat is not it. Fix: signed ticks. Diagnostics
  anyway: stdout line-buffered, a step printed as each starts, and a 100 s watchdog that prints
  the step, each loop's turning/blocked/stopped state and counters, and every stack (`sample`
  on macOS, `gdb` on Linux with `PR_SET_PTRACER`), then exits 3.
- F3 (`6b2c6fa`). `refill`: 40 KiB frames against a 64 KiB window go one at a time, so any
  lost credit wedges the group; a marker must arrive after 1 s of refusals and after the
  receiver restarts with a frame outstanding. Mutants `no-credit-on-refuse`, `no-reset-on-end`
  are in `mutant_commit.py` (which now takes several edits per mutant).
- `stuck-spans-flush` stays killed: 261117-061930-8a6ec4, `slow_reader` 12 of 64 frames,
  dialer failed at 398 ms (a flush that waits for one drain at its end still expires).
- N3 not done: a tighter `upload` gap bound needs Linux numbers first (CI prints a passing
  case's output nowhere).

## Fix round 1 jobs

At `6b2c6fa` (superseded, kept for F2/F3): suite 261117-061020-b68f74 passed; `refill`
261117-061030-b1892b passed; mutants `no-credit-on-refuse` 261117-061106-10ebae and
`no-reset-on-end` 261117-061111-ec7e09 fail `refill` (exit 8); sleep_until probe
261117-061200-bf578c: signed tick slept 0 ms, unsigned killed by the 3 s alarm (exit 142).
At `c050123`: suite 261117-061637-0e42df; cases `refill` 061640-d3fe59, `reader`
061646-ac84a6, `threads` 061651-703f89, `credit` 061657-7baeb3, `upload` 061702-881206, `driver`
061709-b22f6a, `connection` 061915-a84d63; mutants (`--fails-without`) `stuck-ignores-drain`
061722-5deb78, `no-credit-on-refuse` 061730-bc6eb9, `no-reset-on-end` 061738-572024,
`bulk-ahead-of-control` 061749-92f169, `no-credit` 061758-f087b3, `reader-loop-delivers`
061808-cec7df, `no-hand-off` 061815-051c26; TSan 061836-5144dd, TSan reader/upload
061846-41a9fa (races only); stress (reader+upload two at a time x4, `taskpolicy -b`)
061854-7f8033; probe 061903-02bdd4; `stuck-spans-flush` 061930-8a6ec4. All ids 261117-.
Results: suite, every case and `connection` passed; every mutant failed its case (exit 8).
`connection` a84d63: `coarse_writer` 48 of 48 in 3,180 ms, `slow_reader` 64 of 64 in 1,790 ms,
`busy_reader` 6 of 6; `stuck-ignores-drain` 5deb78: `coarse_writer` 6 of 48, dialer failed
at 399 ms. `refill` d3fe59: 100 refused then a marker; a marker after the restart. `reader`
ac84a6 gap 104 ms; `upload` 881206 gap 229 ms; stress 7f8033 (E-cores, two at a time, x4)
passed, worst gaps upload 601 ms, reader 262 ms. TSan 5144dd (threads, credit, driver, refill,
connection, links) and 41a9fa (reader gap 103 ms, upload 237 ms) passed with no report.

## Evidence

Head consensus `81339c2`. Suite 261117-050041-9d19b2. Cases (Release): `threads`
261117-050055-032a41, `credit` 261117-050100-6bc00a, `reader` 261117-050103-ed123d (A1),
`upload` 261117-050107-41c3d7 (A2), `driver` 261117-050110-b75526 (A3). TSan (`groups_threads`,
`groups_credit`, `groups_driver`, `connection`, `links`) 261117-050114-a4b2ed (A1). Mutants
(`test/transport/mutant_commit.py 81339c2 NAME`), each `--fails-without` on the plain case:
bulk-ahead-of-control `f58c8af` on `upload` 261117-050128-1bc50d (A2); no-credit `49b7503` on
`credit` 261117-050133-bf0e3f; reader-loop-delivers `223d7d9` on `threads`
261117-050139-527d2f; no-hand-off `b8726e2` on `threads` 261117-050145-d84fdc (A1).

Values from the job logs: `reader_test: group2_heartbeats=51 group2_longest_gap_ms=103
group1_delivered=200 group1_refused=300` (ed123d); `credit`: the reader held 2 of 40 frames
during the 1.5 s stall (bound 4), group 2's longest gap 60 ms (6bc00a; no-credit: 39 of 40);
`upload`: 128 MiB in 4,009 ms, 55 heartbeats and votes, longest gap 195 ms (41c3d7;
bulk-ahead-of-control: 3, gap 4,005 ms); `threads`: 10,240 of 10,240 in order on their loops,
26 reads, 78 cross-loop posts, 98.5 frames a post (032a41; reader-loop-delivers: 7,440
misplaced; no-hand-off: the link count assertion); `driver`: two groups commit on all three,
the leader (node 1) restarts, both commit again on all three (b75526). TSan passed `groups_threads`,
`groups_credit`, `groups_driver`, `connection`, `links` (a4b2ed).

| Criterion | Evidence |
|---|---|
| A1 | 261117-050041-9d19b2, 261117-050055-032a41, 261117-050100-6bc00a, 261117-050103-ed123d, 261117-050114-a4b2ed; fails without: 261117-050133-bf0e3f, 261117-050139-527d2f, 261117-050145-d84fdc |
| A2 | 261117-050041-9d19b2, 261117-050107-41c3d7; fails without: 261117-050128-1bc50d |
| A3 | 261117-050041-9d19b2, 261117-050110-b75526 |

## Result

consensus gains `include/consensus/transport/host_transport.h` (410 lines) and
`docs/mechanisms/host-transport.md`: one Control and one Bulk connection per peer node, each on
the loop its node places on (journal's `host_detail::slot`/`loop`, `(slot x L) >> 12`); an
accept moves to the owning loop after TLS (`Connection::rehome`, `Links::adopt`). Frames are
kind + group + source and destination incarnations; the reader batches by group loop and posts
once per read (`Connection::on_read`); senders keep per-group control and data queues per
connection, credit then control then data, round robin, data within a 4 MiB per-group window,
handed over 64 KiB at a time when the connection drains (`Connection::on_drained`,
`TCP_NOTSENT_LOWAT`). `send_message`/`receive_message` wire the Driver's Sink `Send` and
`receive`. Diff 1,293 lines added, 8 removed (tests 671).

## Rulings I made

- The 16-byte route of D10 is not stored: placement is arithmetic on the id, and a group holds
  queue entries only while it has frames or uncredited bytes, so an idle group costs 0 bytes in
  the transport.
- Frame header is 25 bytes (kind, group, source and destination incarnations): the destination
  is carried now so transport-identity's rule needs no wire change.
- Credit is per (connection, group) on data frames only; control frames and window updates are
  exempt, as HTTP/2 flow-controls only DATA. Credit returns after the group's loop delivered
  the frame, taken or refused; a refused frame is dropped and counted, as D10's reader_test
  prototype assumes. Track C's connection-wide window is not built (documented).
- `receive_message` takes the frame's source incarnation as `authenticated_peer`; which sources
  a group admits is the host's `Deliver` (the tests require source == connection node, fixed
  voters). The per-frame rule is transport-identity's.
- Cross-loop hops use `asio::post` on the loop's executor (handlers destroyed unrun with the
  io_context) rather than `post_call` with a heap argument that a closed scheduler would leak.
- Frames for the reader's own loop are batched and delivered at the read's end like the others
  (one code path), not one by one.
- An accept's hellos get their own handshake deadline after the move (at most two handshake
  timeouts in all); the pre-authentication bounds still apply to the TLS handshake.
- `TCP_NOTSENT_LOWAT` is not set (fix round 1).
- The stuck window is one write wait, not the time since the last write that made progress: a
  writable wake after WANT_WRITE means the queue drained, so the waits are the progress events.
  `Connection::drained_expiries()` counts drained expiries (a metric, and the test's proof).
- `mutant_commit.py`: `no-extension` and `twice` were already stale at main (their search
  predates `late-always-extends`); `abandon-keeps-dial` matched twice after this quest's accept
  path. Fixed in `ff0d58b` (my file set).
- Connections stay up until `stop()`; closing them when no group shares the peer is
  transport-idle-cost's.
- A3's nodes use one in-memory journal per loop (journal's `MemoryIO` is single-threaded), with
  each loop's Driver on journal loop 0; the transport places groups across the node's 2 loops.
