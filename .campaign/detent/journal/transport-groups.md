# transport-groups: journal

State: waiting on jobs 261117-050041-9d19b2 261117-050055-032a41 261117-050100-6bc00a 261117-050103-ed123d 261117-050107-41c3d7 261117-050110-b75526 261117-050114-a4b2ed 261117-050128-1bc50d 261117-050133-bf0e3f 261117-050139-527d2f 261117-050145-d84fdc
Quest: .campaign/detent/quests/transport-groups.md (the lead's; never edit it)

## Doing

Jobs queued at consensus `81339c2` (code worktree consensus-wt/detent/transport-groups). Local
runs of all five cases, `links` and `connection` passed; the bulk-ahead-of-control mutant was
seen to fail `upload` locally (gap 4,007 ms).

## Next

1. Read the jobs; paste `campaign brief`'s table under Evidence; set "ready for review".

## Blocked on

Nothing.

## Evidence

Head consensus `81339c2`. Suite 261117-050041-9d19b2. Cases (Release): `threads`
261117-050055-032a41, `credit` 261117-050100-6bc00a, `reader` 261117-050103-ed123d (A1),
`upload` 261117-050107-41c3d7 (A2), `driver` 261117-050110-b75526 (A3). TSan (`groups_threads`,
`groups_credit`, `groups_driver`, `connection`, `links`) 261117-050114-a4b2ed (A1). Mutants
(`test/transport/mutant_commit.py 81339c2 NAME`), each `--fails-without` on the plain case:
bulk-ahead-of-control `f58c8af` on `upload` 261117-050128-1bc50d (A2); no-credit `49b7503` on
`credit` 261117-050133-bf0e3f; reader-loop-delivers `223d7d9` on `threads`
261117-050139-527d2f; no-hand-off `b8726e2` on `threads` 261117-050145-d84fdc (A1).

Local values (macOS, light hold, before the jobs): `reader_test: group2_heartbeats=51
group2_longest_gap_ms=103 group1_delivered=200 group1_refused=300`; `credit`: the reader held 2
of 40 frames (window 3), group 2's longest gap 59 ms; `upload`: 126 MiB in 4,005 ms, 55
heartbeats and votes, longest gap 226 ms (mutant: 3, 4,007 ms); `threads`: 10,240 of 10,240,
26 reads, 78 cross-loop posts, 98.5 frames a post.

| Criterion | Evidence |
|---|---|
| A1 | pending: jobs above |
| A2 | pending: jobs above |
| A3 | pending: jobs above |

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
- `TCP_NOTSENT_LOWAT` (64 KiB) is set by `Connection` itself, for every host connection.
- Connections stay up until `stop()`; closing them when no group shares the peer is
  transport-idle-cost's.
- A3's nodes use one in-memory journal per loop (journal's `MemoryIO` is single-threaded), with
  each loop's Driver on journal loop 0; the transport places groups across the node's 2 loops.
