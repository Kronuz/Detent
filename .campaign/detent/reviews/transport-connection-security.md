# transport-connection review: security

Commit: consensus:35ebaba85bc08e37d5129ad5709c7cfbf7bd8b2c
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

All at consensus:35ebaba unless noted, every one read from its log, not the journal:

- Suite 261117-012645-510cf4 (exit 0); `deadline` 261117-012648-223239, `connection`
  261117-012653-af6f61, `node_hello` 261117-012657-190319, `links` 261117-012701-805798 (all exit 0).
- TSan 261117-012705-7fdd18: `deadline`, `connection`, `links` all passed under
  `-fsanitize=thread`, `halt_on_error=1`.
- 15 mutants at consensus:c1270f0, each exit 8 with the targeted assertion firing (spot-checked
  `pin-self` connection_test.cc:200, `refuse-when-offered` links_test.cc:167, `entry-left-armed`
  deadline_test.cc:156). consensus:c1270f0..35ebaba touches only `links.h` (the destructor) and
  `links_test.cc`, so they stand for the head.
- Fuzz 261117-012148-792d04 at consensus:c1270f0: real libFuzzer with ASan and UBSan
  (`-fno-sanitize-recover=all`, CMakeLists.txt:123), `FUZZ_DECODER=9` pins every input to
  `decode_node_hello`, 70,928,474 runs in 1,201 s, no crash; coverage settles at 74 edges after
  about 4,000 runs, as expected of a fixed-offset 64-byte decoder. `node_hello.h` is unchanged
  since.

## What I checked myself

Probe job 261117-014504-8a23df (exit 1: every case showed its defect), from
`.scratch/review-transport-connection-security/` in the Detent home: `review_probe_test.cc` and
`run.sh`, which adds the probe to a fresh checkout's CMake and runs three cases. Rerun with
`campaign job submit --quest transport-connection --fresh --force -- sh <dir>/run.sh <dir>`; it
exits 0 once S1 and S2 are fixed.

By reading, against the six questions:

1. Authentication. `Connection::handshake` returns only after `SSL_do_handshake` completes and
   `NodePins::authenticate` passes (connection.h:64-73); `Links::accept` and `Links::offer` touch
   no hello before it (links.h:99, 161). The checks cover a verified leaf (verify mode and result,
   then a leaf present, since a missing certificate verifies OK), pinned, not this node's, and the
   dialed node (node_pins.h:33-51); tests assert each refusal's text and three mutants are killed.
   Renegotiation is off in the context (`SSL_OP_NO_RENEGOTIATION`, reactor_tls.h:68, TLS 1.2
   minimum), TLS 1.3 post-handshake auth is never enabled, and nobody calls `SSL_do_handshake`
   twice; a renegotiation would also need the other certificate's private key. An SSL error
   mid-read throws in `want()` (connection.h:140-143), clears the queue and fails the connection,
   and `SSL_shutdown` is skipped once failed. What is wrong is the order of close and SSL use
   after a failure: S1.
2. DTHL0002. Exact 64-byte size before any read, fixed offsets with no length field, reserved
   flags refused, enum validated after the cast (defined for a `uint32_t` enum), every field
   checked by `node_hello_fault` (node_hello.h:30-79); no out-of-bounds read or overflow is
   possible. The fuzz job is real (above).
3. Tie-break. I traced each order of the two offers and two accepts (`wins`, links.h:267-273;
   the accept reserves before its reply, links.h:169-171; the offer checks `now.offered != link`,
   links.h:127): both ends keep exactly the lower node's dial; neither keeps both. "None" happens
   only transiently, when the peer still holds a link this end already lost (until `dead_after`).
   A draining link is excluded through `find()` (links.h:72-76) and its later `release` compares
   identity (links.h:282-286), so the new link is not cleared. The pre-hello give-up leaks: S2.
4. Lifetime. `Lookup::abandon` and the delivery take the same mutex (resolve.h:90-105), so once
   `~Links` or `stop()` returns, no resolver thread posts; the thread then touches only its own
   captures, and a posted answer checks the weak `Shared` first (links.h:324-328). The product has
   no path like the test bug fixed at consensus:35ebaba, as long as the resolver captures nothing
   it does not own (N5).
5. Late deadline. One class (`deadline.h`), used by every transport wait (handshake, hellos,
   read, stuck write, keepalive, lookup, connect race); there is no other timer in
   `connection.h` or `links.h`. Extends once, by the lateness, capped at one timeout
   (deadline.h:74-89). A wake before `wait()` is kept in `state_` and `await_ready` sees it; a wake
   after an extending fire cancels the re-armed entry. The wheel allows a fire to cancel or re-arm
   any entry (wheel.hh:148), which `race()` relies on. On "one tick" see N3.
6. Asio rule. `wake()` and the fire post the waiter (deadline.h:90-93); `await_asio` stores a
   handler that runs inside `init` and resumes after `await_suspend` (task:95798a0
   docs/mechanisms/await-asio.md); the connect race's handlers only call `wake_all`. No completion
   runs inside its initiating function.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | yes | defect | `fail()` closes the fd while `SSL` still holds it (`SSL_set_fd`), and the reader and writer keep calling `SSL_read`/`SSL_write` on that number afterwards: the writer after `abort()` or a reader failure with a frame queued (its posted resume swaps `out_` and flushes), the reader after a frame handler aborts. Once another thread's socket or file takes the number, this connection writes TLS records onto it and consumes its bytes: another peer's stream or a storage file corrupted. | Probe 261117-014504-8a23df: `fd_reuse_writer` wrote a 49-byte record onto the socket that took fd 8; `fd_reuse_reader` consumed 5 of its bytes and wrote a 24-byte alert onto it. Code: connection.h:129-136 (close), 226-230 (flush after wake), 205-209 (fill after deliver). | Never close the fd before `run()` returns: `fail()` does `::shutdown(fd, SHUT_RDWR)` and `socket_.cancel()`; the close stays in `run()`'s end and the destructor; and no `SSL_*` call once `end_ != open` (check in `fill()` and `flush()`). Test: the two probe cases as `connection_test` cases (`dup2` a socketpair onto the old number), asserting the stranger's bytes unread and nothing written. |
| S2 | yes | defect | `offer()`'s early return (a link already established, or stopping) leaves the dialed, TLS-authenticated connection in `live_` with its socket open until `stop()`: one fd and one `SSL` per lost race for the host's lifetime, and the peer's accept holds one of its 4 handshake slots for the 5 s deadline. The race is a normal one (both ends dial after a link drops), so fds grow with link churn until `EMFILE`. | Probe 261117-014504-8a23df `abandoned_dial`: offer returned false, `abandoned` 1, the connection still held with its socket open after the caller dropped it. Code: links.h:114-117. | On that path `connection->abort(); forget(connection);` as the later failure path does (links.h:140-143). Test: the probe case in `links_test`, asserting a `weak_ptr` to the given-up connection expires. |
| N1 | no | gap | Pre-authentication slots: 4 handshakes at once, each up to `handshake` (5 s), more closed (links.h:149). An unauthenticated network peer holding 4 idle TCP connections, refreshed every 5 s, refuses every incoming link to the host; doing it to both ends of a pair keeps them apart. `Peers` has the same shape with 2. | links.h:149-154, peers.h:491 | Owner's call: keep it; or a shorter deadline for the TLS phase, a per-source-address cap, or replacing the oldest handshake instead of refusing the newest. |
| N2 | no | defect | The stuck-write deadline spans one `flush()` of everything swapped out of `out_`, not a period without progress: a peer reading steadily but slower than `queued bytes / dead_after` (25 MiB queued, 5 MB/s) ends `failed`. The doc says "cannot write for as long". | connection.h:228-231 | Restart the deadline when an `SSL_write` makes progress. |
| N3 | no | nit | "On-time fires never re-arm" is not strictly true: `asio_scheduler::arm` fires "at most one tick plus the loop's wake latency late" (task_asio.hh:131-133), so an on-time fire can measure past one tick and extend by its own small lateness. Safe (never early, once, capped at one timeout), but the journal's ruling overstates it. | deadline.h:80 | Reword the ruling, or allow a tick plus a margin. |
| N4 | no | nit | With `SSL_set_fd`, OpenSSL writes with plain `write`, not Asio's `MSG_NOSIGNAL`: on Linux a write after the peer reset raises `SIGPIPE`. detentd ignores it (Detent src/detentd/main.cc:185); the header does not say the host must. | connection.h:45 | Say so in the header, or set `SO_NOSIGPIPE` where it exists and require `SIG_IGN` elsewhere. |
| N5 | no | gap | `~Links` abandoning lookups (consensus:575671e) is sound by the shared mutex but untested; and `LinkOptions::resolver` runs on a thread that may outlive `Links`, which its comment does not say (the test bug fixed at consensus:35ebaba was exactly this). A deterministic test exists: a resolver blocked on a latch that the case releases after `~Links` and the `io_context` are gone, under ASan. | links.h:58-63, resolve.h:80-105 | Document the resolver's lifetime; add that test if wanted. |
| N6 | no | nit | `Connection` trusts the caller's context policy (peer verification with `fail_if_no_peer_cert`, no renegotiation); `authenticate` still refuses a missing or unverified leaf, so nothing is bypassed today. | connection.h:35-50 | Optionally assert `SSL_OP_NO_RENEGOTIATION` and `SSL_VERIFY_PEER` on the context at construction. |
| N7 | no | nit | `NodePins::authenticate` repeats `authenticated_voter`'s verify-and-match (tls.h:47-66): two pin matchers. Acceptable while `Peers` lives (D106). | node_pins.h:33-51 | Fold into one when worker-pump-removal deletes `Peers`. |
| N8 | no | nit | A retiring end's redial is refused while the peer has not yet read its `close_notify`, which waits behind the queued frames: the doc's "a new connection of the lane may be dialed" holds only once the peer sees the drain. Transient; the caller redials. | links.h:267-273 | Say so in host-connection.md. |

## Not run

- No ASan or UBSan build of `connection` or `links` (the fuzz job covers the decoder only); TSan
  is the quest's one local sanitizer, and Linux and the sanitizers are CI's, which I did not read.
- No Linux run, so N4 is from reading; N1, N2 and N8 are from reading, with no probe.
- No test of `~Links` with a live resolver thread (N5).
- No adversarial TLS client (a malformed handshake, KeyUpdate floods); OpenSSL's own handling is
  assumed.

# transport-connection review: security, round 2

Commit: consensus:2f6f210597ae3fce94beacedfca90172381cf312
Verdict: APPROVE
Date: 2026-11-17

Round 2 re-checks round 1's CHANGES at the fix head: `git diff 35ebaba 2f6f210` (6 files, 391
lines: connection.h, links.h, host-connection.md, connection_test, links_test, mutant_commit.py).

## Evidence I relied on

All at consensus:2f6f210, every one read from its log:

- `connection` 261117-020944-e6a5b4 and `links` 261117-020957-62fbbb (exit 0). The transport tests
  build with `-UNDEBUG` (CMakeLists.txt:151-163), so their asserts are live in the Release jobs.
- TSan 261117-021002-3a215c; g++-16 `connection` and `links` 261117-021006-6e7652 (exit 0).
- The new mutants, each exit 8 at the case's own assertion: `fail-closes-fd` 261117-021146-625f2d
  at `port_of(old) == port` (connection_test.cc:446, the fd still this socket's after `abort()`);
  `ssl-after-end` 261117-021151-7f0a64 ("20 unread, 24 written onto it"); `stuck-spans-flush`
  261117-021157-d2b65d (`slow_reader`); `abandon-keeps-dial` 261117-021226-2e4b1a ("still held");
  `refuse-newest` 261117-021232-a56084 and `no-source-cap` 261117-021238-0c6d91 (`flood`'s eviction
  count); `dial-one-expression` 261117-021243-1aac9a (g++-16, the `dial` assertion).
- My round-1 probe rerun by the contributor, 261117-021249-cfd240: `abandoned_dial` now exits 0
  ("released, its socket closed"). Its two fd cases exit 1 by the probe's construction, not by a
  defect: they `dup2` the stranger onto a number the connection now still owns and keep no second
  fd, so `run()`'s close takes the stranger's only descriptor. The round-2 probe replaces them.

`fd_reuse_reader` and `fd_reuse_writer` are real tests: each asserts the fd is still this socket's
right after `abort()` (killed by `fail-closes-fd`), then puts a stranger on the number with a
second fd kept, and asserts its 25 bytes unread and nothing written (killed by `ssl-after-end`).

## What I checked myself

Probe job 261117-022229-d28475 (exit 0, 78 s), from `review_probe2_test.cc` and `run2.sh` in
`.scratch/review-transport-connection-security/` in the Detent home. Rerun with
`campaign job submit --quest transport-connection --fresh --force -- sh <dir>/run2.sh <dir>`.

- `after_end`, 3 runs: the handler aborts on the first of three frames that arrive in one record;
  the fd is still this socket's after `abort()`, the handler is called once (the `deliver` guard),
  and the stranger's 25 bytes stay unread with 0 written onto it. Every run: end `failed`.
- The reader's yield is guarded: `slow_reader` passes 3 of 3 at the head (64 of 64 frames, about
  1.75 s) and fails 3 of 3 with the `co_await task::yield()` line deleted (11 to 13 of 64 frames,
  dialer `dead` at about 350 ms), on clang here as well as the contributor's gcc.
- `churn RATE`: idle TCP connections from 127.0.0.1 (the real dialer's own address, so the
  per-source cap applies and 2 newer arrivals evict a real handshake) held until 200 newer exist,
  while node 1 dials node 2, at most 20 tries. Unthrottled (3,236 connects/s from one thread): 0 of
  20 dials linked in 0.29 s, 2,533 evicted. About 1,000/s: linked on the 2nd try; 1,356/s: the 5th;
  159/s and below: the 1st.

By reading, against the questions:

1. S1. `fail()` sets `closing_`, then only on the first end sets `end_`, `::shutdown(SHUT_RDWR)`
   and `socket_.cancel()` (connection.h:136-145); a second `fail()` touches no socket. The fd is
   closed only at the end of `run()` (connection.h:96-98) and by the socket's destructor; nothing
   else calls `close`. `run()` returns only after both coroutines finish, and `end_` is never
   `open` then, so a later `abort()` from `Links` is a no-op on the number. Every SSL call is behind
   `open_or_throw`: `handshake` (71), `fill` (171), `flush` (190), and after every wait in `want`
   (164); `SSL_shutdown` runs only when `end_ == open` (270) and its retries pass through `want`.
   A cancelled wait throws on its error code before reaching SSL. `deliver` stops at the end (210),
   and the reader's new yield is followed by `deliver` (guarded) and `fill` (guarded). Resolved.
2. S2. `offer()`'s give-up now aborts and forgets (links.h:120-126). The other give-ups were already
   released: `connect()` forgets on a throw or on stopping (destroyed with its socket), and
   `offer()`'s later failure path aborts and forgets. Resolved.
3. The new mechanisms:
   - N2. `flush(.., true)` returns when a write made progress and the next must wait; the writer's
     loop then builds a fresh `late_deadline` (connection.h:254-257), so `dead_after` now bounds
     time without progress. The retry passes the same pointer and length, as OpenSSL requires after
     `WANT_*` (and `SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER` is set). Progress is one record, so a peer
     reading at least about 16 KiB per `dead_after` (about 3.3 KB/s) keeps the connection; only an
     authenticated node gets there, and the queue's bound is the caller's (`queued()`). Sound.
   - The yield. `task::yield` posts the handle through `asio::post` (task:95798a0 task.hh:1025-1033,
     task_asio.hh:76-80), behind the timers and the writer's wake already queued, so a steady inflow
     no longer runs the reader without a loop turn. One post per record (16 KiB). Sound, and guarded
     (probe above).
   - `flush` advances `sent_` instead of erasing the front; `queued()` subtracts it; `sending_` is
     released when done and `sent_` reset, and nothing replaces `sending_` mid-flush (the writer
     swaps only when it is empty; `write_hello` runs once, before `run()`). Sound.
   - The pre-authentication pool. `admit` runs before the TLS handshake, evicts the source's oldest
     when the source is at its cap, else the pool's oldest when full, and aborts it; the evicted
     task's wait is cancelled and it forgets its connection. `authenticated` drops the entry when
     the pins pass, before the hellos, and again on every exit. A one-address flooder now keeps 2
     slots and cannot lock a real dialer out; `flood` shows it. Replace-oldest under churn: N9.
   - SIGPIPE. `SO_NOSIGPIPE` where it exists; the header and the doc say the host ignores `SIGPIPE`
     on Linux; detentd does (Detent src/detentd/main.cc:185) and both tests do. N4 resolved.
   - gcc. `dial()` splits `co_await offer(..)` out of `&&` (links.h:85-90), same meaning; the
     g++-16 mutant shows the one-expression form fails. No other `co_await` sits in an operand of
     `&&`, `||` or `?:` in `include/`.

On the churn question: the handshake deadline bounds each attacker connection to 5 s, not the
attack. What the attacker must sustain is the pool's size per handshake time: 4 connects per
handshake from addresses below their cap (2 from the real peer's own address, as behind a NAT).
On loopback that is thousands per second (measured above); across a WAN the acceptor's handshake
lasts at least one round trip, so at 100 ms it is about 40 connects per second, which is cheap. It
must also hit both ends, since an unflooded end's own dial is accepted by the flooded one's tie-break
(`wins` with no own offer). Round 1's refuse-newest needed one connect per second; this is strictly
better, and any bounded pre-authentication pool has such a rate. I judge it acceptable to land,
with the doc corrected (N10); keeping out an unauthenticated flood belongs to the network
(an allowlist of the cluster's addresses), or to N9's option.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| S1 | no | defect | Resolved. The fd closes only when `run()` returns; `fail()` ends once with shutdown and cancel; no SSL call and no frame after the end. | connection.h:96-98, 136-149, 164, 171, 190, 210; `fd_reuse_reader`, `fd_reuse_writer` with mutants 261117-021146-625f2d, 261117-021151-7f0a64; probe 261117-022229-d28475 `after_end` 3 of 3 | None. |
| S2 | no | defect | Resolved. The given-up dial is aborted and forgotten. | links.h:120-126; `abandoned_dial` with mutant 261117-021226-2e4b1a; probe 261117-021249-cfd240 `abandoned_dial` exit 0 | None. |
| N9 | no | gap | Replace-oldest can be churned: new connections from addresses below their cap evict a real handshake that has not authenticated within 4 (or, from its own address, 2) arrivals. The rate needed is about pool size per handshake time: thousands per second on loopback, about 40 per second at a 100 ms round trip, at both ends. | Probe 261117-022229-d28475 `churn`: unthrottled 3,236/s from 127.0.0.1, 0 of 20 dials linked; 1,000/s linked on the 2nd try. links.h:328-342 | Owner's call: keep it (documented, the network's allowlist does the rest); or never let an unknown source evict a connection whose source is a configured peer's address (`routes`), which makes the flood originate from a peer's address; or a larger pool. |
| N10 | no | nit | The doc says the attacker must open connections "faster than a handshake takes (milliseconds)"; across a WAN a handshake is one or more round trips, so tens per second suffice, and from a NAT-shared address 2 arrivals, not 4. | host-connection.md, Before authentication | State the rate as the pool's size per handshake time, round-trip bound. |
| N11 | no | nit | The per-source cap keys on the exact address; an IPv6 attacker holds a /64 of sources, so there the pool of 4 is the only bound. | links.h:329-331 | Optionally key IPv6 sources by /64; or say so in the doc. |
| N12 | no | nit | A connection leaves the pool once its pins pass, before the hellos: a pinned node can hold any number of hello-phase connections, each up to `handshake` (5 s). Round 1 counted them to the end of `accept`. Only an authenticated node gets there. | links.h:173, 192 | Keep, or count the hello phase per authenticated node. |
| N13 | no | gap | No suite test asserts that no frame reaches the handler after the end (`deliver`'s guard); `ssl-after-end` does not remove it. My probe shows it holds. | connection.h:210; probe `after_end` "handler calls 1" | Count handler calls in `fd_reuse_reader` (send the frames in one `loop.call`) and assert 1. |

Round 1's N2 and N4 are resolved by the fix; N5's lifetime comment is in (links.h:19); N8 is in the
doc; N3, N6 and N7 stand as round 1 left them.

## Not run

- CI at 2f6f210 (Linux, the sanitizers) was still running; I did not read it. The SIGPIPE and gcc
  fixes are judged from the code and the g++-16 job, not from Linux.
- No churn from several distinct addresses (macOS loopback has only 127.0.0.1), and none over a
  link with real round-trip time; the 40/s figure is arithmetic from the code, not a measurement.
- No ASan or UBSan run of `connection` or `links` locally (CI's).
