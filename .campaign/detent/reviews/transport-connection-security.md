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
