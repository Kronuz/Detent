# transport-connection: journal

State: waiting on jobs 261117-032548-ab81be 261117-032550-6206a7 261117-032553-0e82fd 261117-032556-fc0b12 261117-032600-4ce9d8 261117-032604-d35a2f 261117-032608-c60abe 261117-032611-97f801 261117-032620-2cee82
Quest: .campaign/detent/quests/transport-connection.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Built at consensus `35ebaba` (8 commits on main `57745e8`): `deadline.h`, `node_hello.h`,
`node_pins.h`, `connection.h`, `links.h`, their tests, `docs/mechanisms/host-connection.md`,
CHANGELOG. At `c1270f0`: suite and the four test jobs passed, all 15 mutants killed (exit 8),
TSan failed on a test bug (the lookup case's abandoned resolver thread wrote a counter on a
dead stack; fixed at `35ebaba`). `575671e` makes `~Links` abandon a refreshing lookup.

## Fix round 1 (security review CHANGES)

- S1: `fail()` ends once: `shutdown(SHUT_RDWR)` and `cancel()`, no close; the fd closes when
  `run()` returns. `open_or_throw` before every SSL call in `fill`/`flush`/handshake and after
  every wait; `deliver` hands no frame after the end. Cases `fd_reuse_reader`, `fd_reuse_writer`
  (the reviewer's probes, the stranger dup2'd onto the old number, a second fd kept to read it).
- S2: `offer()`'s give-up aborts and forgets the connection. Case `abandoned_dial` (weak_ptr).
- N2: the writer's stuck deadline restarts at every write that made progress (`flush(.., true)`
  returns early). Case `slow_reader`: two loops, 64 x 512 KiB at 20 ms per frame, dead_after 300 ms.
- Found by `slow_reader` (gcc build: dialer `dead`, 60 of 64 frames, 4.2 s, probe
  261117-020305-7edbae): the reader never suspended while data kept arriving, so its own writer's
  keepalives starved and the peer declared it dead; it now yields one loop turn per read. And
  `flush` erased each partial write from the front of the queue (O(n^2) memmove on a 32 MiB
  queue); it now advances an offset and releases the buffer when done.
- `fail()` touches the socket only on the first end: a second `fail()` (the reader's catch)
  shut down whatever held the number by then (probe 261117-020156-7424e1).
- N1: `Links::admit`: pool of 4, at most 2 per source address, a newcomer replaces the source's
  oldest, else the pool's oldest; a handshake leaves the pool once authenticated. Case `flood`.
- CI at `35ebaba` was red on Linux: `connection` died of SIGPIPE (N4: OpenSSL `write()`), and
  `links` `dial` failed on gcc only (13.3 on CI; reproduced with g++-16 here, probe
  261117-015213-31e196). Fixes: `SO_NOSIGPIPE` where it exists, the tests ignore SIGPIPE, the
  header says the host must; `dial()` no longer puts `co_await offer(.., std::move(connection))`
  in the right operand of `&&`; mutant `dial-one-expression` built with g++-16 shows it.
- Mutants added: fail-closes-fd, ssl-after-end, stuck-spans-flush, abandon-keeps-dial,
  refuse-newest, no-source-cap.

Fix head: consensus `2f6f210` (`f0a50db` connection, `48ba519` links, `2f6f210` doc and mutants).

## Fix round 2 (CI macos-x86_64: `slow_reader` dialer dead, 7 of 64 frames in 643 ms)

Cause: the test. Both ends share one process, so a host stop silences the reader's keepalives
too, which no two hosts do. Forced at `2f6f210` (probes, working tree = head, light):
- no stop 0 of 10 failed; CPU starvation (2 x ncpu spinners, strict) 0 of 15
  (261117-023507-f1cb6b, 261117-023812-a2fe7a): the stuck deadline and keepalives hold.
- SIGSTOP 200-400 ms, 100-900 ms in, fixed seed: 16 of 40 failed, 15 "dialer 3, acceptor 2"
  (CI's signature), one dialer `failed` (261117-023507-f1cb6b). Deaths cluster at stops of
  204-319 ms: the dialer's dead deadline lands just after the stop ends, too little late to
  extend, while the reader's own keepalive timer fired late and extended.
- Same stops, keepalive timer without the late extension (probe-only patch, reverted): 1 of 40
  (261117-023724-01de87). See "For the lead".
Fix (consensus `76426af`, test only): `stall_watch` (a thread sleeping 1 ms records the longest
gap); a failed `slow_reader` run the host stalled >= 100 ms (a third of dead_after) reruns, at
most twice; only a whole run passes; a failure without such a stall fails at once. Same 40 stops:
0 failed, 13 retried, each retry whole (probe 261117-024148-dffbff); plain runs report 1 ms
stalls. `test/transport/slow_reader_stops.py` forces the stops. No product change.

## Fix round 2b (CI at `76426af`: dialer dead again, no stall; msg from the lead)

State 3 is `dead`: the dialer's reader heard nothing from the acceptor for dead_after (the
stuck write ends `failed`, 2). Cause: the product. The keepalive interval took the late rule's
extension, so on a reader whose loop is busy longer than the interval the fire comes late,
extends, comes late again inside the next busy turn, and only then sends: every other turn.
Probes at `76426af` with trace (261117-030425-64d591, 261117-031100-13184a; probe-only patch,
reverted): handler 150 or 200 ms per frame, dialer dead 12 of 12 (trace: keepalive held 50 ms
by the extension, sent 13 ms after the dialer's 300 ms expired); without the extension 0 of 12,
64 keepalives for 64 frames. 120 ms per frame passed both ways (gap ~250 ms, thin).
Fix, consensus `677c864` (product, goes back to the reviewer): `late_deadline(loop, timeout,
bool extend_late = true)`; `on_wheel` extends only if `extend_late_`; `Connection::writer`'s
idle wait passes false. Tests: `deadline_test` `late_expires`; `connection_test` `busy_reader`
(6 x 64 KiB, 400 ms per frame, dead_after 600 ms) on `slow_reader`'s harness (`read_slowly`,
stall rerun at dead_after / 3). Mutants `late-always-extends`, `keepalive-extends`. Doc: the
late rule paragraph and the checks table.
Jobs 261117-024445-1c6a25..-024521-a9f6d0 exited 127 (`$Q` was one word in zsh); resubmitted
at `677c864` instead.
CI `consensus_transport_peers` abort: `peers_test.cc:1196` frozen-peer retry check, the Peers
fetch flake on macos-x86_64 (D102); same check failed at `35ebaba` (run 44055780068) and on
consensus-fetch-macos `38f2022` (run 43839418972, then line 1244). This branch does not touch
`peers.h` or `peers_test.cc`. Pre-existing, the lead's.

## Next

1. Read the jobs in State; send `677c864` to the reviewer (product change); push for CI.

## For the lead (review notes not fixed here)

- N5: the resolver's lifetime is now documented on `LinkOptions::resolver`; the deterministic
  `~Links`-with-live-resolver test (latch, ASan) is not written.
- N6: `Connection` does not assert the context's verify mode and `SSL_OP_NO_RENEGOTIATION`.
- N7: two pin matchers (`NodePins::authenticate`, `authenticated_voter`) until `Peers` goes (D106).
- Keepalive and the late rule: fixed in `677c864` (fix round 2b).
- Stuck-write progress counts whole TLS records (`SSL_write` > 0). With 16 KiB socket buffers
  (smaller than a record) a record took 100-300 ms on loopback and slow_reader ended dialer
  `failed` 12 of 12, with or without the keepalive fix (probe 261117-031100-13184a). Default
  buffers (and 32 KiB, 64 KiB) pass; production's dead_after is 5 s. Counting socket-level
  progress needs `SO_NWRITE`/`SIOCOUTQ`; not done.

## Blocked on

Nothing.

## Evidence

At `677c864` (fix round 2b): suite 261117-032548-ab81be; `deadline` 261117-032550-6206a7;
`connection` 261117-032553-0e82fd; TSan 261117-032556-fc0b12; mutants, each `--fails-without`:
keepalive-extends 261117-032600-4ce9d8, stuck-spans-flush 261117-032604-d35a2f,
late-always-extends 261117-032608-c60abe; 40 forced SIGSTOPs 261117-032611-97f801; `connection`
x6 beside 2 x ncpu spinners (strict) 261117-032620-2cee82.

At `76426af` (fix round 2): suite 261117-024444-856be1; `connection` 261117-024445-1c6a25; TSan
261117-024447-c8c390; mutant stuck-spans-flush (`a8914e5`) 261117-024515-fdecab; 40 forced
SIGSTOPs 261117-024518-eb8beb; `connection` x10 beside 2 x ncpu spinners (strict)
261117-024521-a9f6d0.

At `2f6f210` (fix round 1): suite 261117-020930-674e37; `deadline` 261117-020940-345e4b,
`connection` 261117-020944-e6a5b4, `node_hello` 261117-020950-becb2a, `links` 261117-020957-62fbbb;
TSan 261117-021002-3a215c; g++-16 `connection` and `links` 261117-021006-6e7652. Mutants at
`2f6f210`, each `--fails-without`: no-extension 261117-021037-4df123, uncapped
261117-021041-dbab12, twice 261117-021046-535d94, wake-ignores-extension 261117-021051-2d4cc4,
entry-left-armed 261117-021055-8114e8, pin-any-node 261117-021101-69bcf2, pin-self
261117-021120-6cfd3d, pin-unpinned 261117-021125-b0806f, close-drops-queue 261117-021130-36cdcc,
no-close-notify 261117-021136-2134e3, no-keepalive 261117-021141-a71c31, refuse-when-offered
261117-021205-c58e50, accept-when-offered 261117-021211-381e3c, draining-counts
261117-021216-ebf15b, lookup-no-wake 261117-021221-3c6fba; new: fail-closes-fd (S1)
261117-021146-625f2d, ssl-after-end (S1) 261117-021151-7f0a64, stuck-spans-flush (N2)
261117-021157-d2b65d, abandon-keeps-dial (S2) 261117-021226-2e4b1a, refuse-newest (N1)
261117-021232-a56084, no-source-cap (N1) 261117-021238-0c6d91, dial-one-expression (gcc, g++-16)
261117-021243-1aac9a.

All passed: suite, four tests, TSan (connection 4.3 s, links 3.1 s), g++-16, and every mutant
exit 8 at its targeted assertion (spot-checked: fail-closes-fd at the fd-still-ours check,
ssl-after-end "20 unread, 24 written", stuck-spans-flush "14 of 64 frames in 426 ms",
abandon-keeps-dial "still held", refuse-newest and no-source-cap at the flood's eviction count,
dial-one-expression at the dial assertion under g++-16).

The reviewer's probe rerun at `2f6f210`: 261117-021249-cfd240 (exit 1, as expected: `abandoned_dial` passes;
`abandoned_dial` passed; the two fd cases print "0 unread, 0 written" by the probe's construction: its stranger is dup2'd onto a
number the connection still owns, which `run()` then closes, and the probe keeps no second fd to
count what is left). "0 written" is the part that holds; `connection_test`'s cases keep that fd.
Probes of the working tree (never evidence): 261117-015858-c9ed5e, 261117-020156-7424e1,
261117-020305-7edbae, 261117-020718-199e81; 261117-015213-31e196 reproduced the gcc `dial`
failure at `35ebaba`.

At `35ebaba` (before the review):

At `35ebaba`: suite 261117-012645-510cf4; TSan 261117-012705-7fdd18. At `c1270f0`: suite
261117-011918-3232f8 passed. Mutants are commits on no branch from
`test/transport/mutant_commit.py c1270f01d233 NAME`; each job is `--fails-without`, all exit 8.

| Criterion | Jobs |
|---|---|
| A1 | `deadline` 261117-012648-223239; `links` (lookup) 261117-012701-805798; mutants no-extension 261117-012000-205e0e, uncapped 261117-012004-35b740, twice 261117-012009-0d6a0f, wake-ignores-extension 261117-012013-dbd982, entry-left-armed 261117-012021-795811, lookup-no-wake 261117-012118-209628 |
| A2 | `connection` 261117-012653-af6f61; mutants pin-any-node 261117-012027-32b241, pin-self 261117-012033-e1cceb, pin-unpinned 261117-012039-9c2239 |
| A3 | `node_hello` 261117-012657-190319; fuzz (FUZZ_DECODER=9, 20 min, at `c1270f0`; the decoder is unchanged since) 261117-012148-792d04 |
| A4 | `links` 261117-012701-805798, `connection` 261117-012653-af6f61; TSan 261117-012705-7fdd18; mutants close-drops-queue 261117-012045-b8df9d, no-close-notify 261117-012052-5bfa60, no-keepalive 261117-012059-910de1, refuse-when-offered 261117-012105-586789, accept-when-offered 261117-012109-4f9a7a, draining-counts 261117-012114-7d6e0c |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- "No DTHL0001 reader kept" applies to the new connection: `node_hello.h` reads only DTHL0002.
  `Peers` keeps its DTHL0001 (`hello.h`) until worker-pump-removal deletes `Peers` (D106).
- DTHL0002 is 64 bytes: node, lane, flags (bit 0 `refused`, the acceptor's tie-break answer),
  cluster, connection id, rpc_min, rpc_max (1..9,999, the lane magic's four digits). Group,
  epoch and profile leave the hello for the group-open exchange (transport-identity).
- Late means more than one wheel tick (1 ms) past the deadline: the wheel's own granularity is
  not a stop. An on-time fire can still measure a little over a tick on `asio_scheduler` (a tick
  plus the loop's wake latency) and extend by that small lateness: never early, once, capped (N3).
- The keepalive is a zero-length frame; the dead-peer deadline (`dead_after`, 5 s) restarts on
  every read wait, and a write stuck for `dead_after` fails the connection.
- Tie-break: a dial counts once its hello is sent; the lower node's dial wins. A dial that
  finds a link established before its hello gives up quietly (asymmetric reachability cannot
  make an unsent dial refuse a working one).
- `slow_reader` reruns a failed run only when the process stalled >= 100 ms (dead_after / 3),
  at most twice: a stop shared by both ends is a test artifact; no run passes unless whole.
- Wall-clock tests assert only bounds a runner's process stop cannot break (never early; open
  under keepalives) and print the timings, so no new entry needs the `timing` label (D100).
  `deadline_test` injects stops on `inline_scheduler`'s manual clock, with no wall clock.
- `consensus_transport` links `task::asio` when the target exists (the connection awaits Asio).
- N1's per-source cap is 2, not the ruling's 1: a node dials Control and Bulk at once from one
  address, and with 1 each lane's accept evicts the other's (`links_test` `threads` dials both
  lanes both ways at once). 2 still leaves a one-address flooder 2 of 4 slots and no lock-out.
  The lead may set `handshakes_per_source = 1` (one line) if lane dials are to be serialized.
- `LinkStats::evicted` counts pre-authentication handshakes replaced; `rejected` now counts only
  connections refused on arrival (stopping, or no peer address).
- N8 is a doc line in host-connection.md (the peer refuses a retiring end's redial until it reads
  the close_notify).
- `~Links` abandons in-flight lookups (`575671e`): a cached name's refresh runs with no task, so
  the drained assertion alone does not stop its thread posting later. No test: it needs an
  io_context destroyed under a live resolver thread, which is not deterministic.
