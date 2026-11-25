# consensus-fetch-oversize-abort: journal

State: ready for review (waiting on jobs 261124-165220-e84fc9, 261124-165224-4e171d and CI 45521429248)
Quest: .campaign/detent/quests/consensus-fetch-oversize-abort.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 2 review at 87cb821 (CHANGES, R2-F1; F1-F4 closed): nothing tested fail() ending a
second ask that waits for its connection connection_aborted. Fixed test-only in
consensus:ce981be: fetch-removed adds fetch_waiting (the reviewer's probes2 shape: the child
host SIGSTOPs in its reader, the link ends dead, the redial's handshake waits; then
remove_node(2) or drain()), asserting connection_aborted within 500 ms. Local 3/3: both 0 ms.
Jobs: 261124-165220-e84fc9 (fetch-removed x10 at ce981be); fails without:
261124-165224-4e171d (mutant/fetch-no-again-map, consensus:6190d84). CI 45521429248. No code
change, so A2 and the suite are not rerun (lead's ruling).

Correctness review at ea48bc8 (CHANGES; .campaign/detent/reviews/consensus-fetch-oversize-abort-correctness.md):
- F1 (blocking, fixed in consensus:87cb821; table first in consensus:afec3fe): lead's ruling, the
  second ask goes out only on a connection that is up. The table is docs/mechanisms/rpc-lane.md,
  "A fetch whose connection ends": each way a connection ends under a fetch x the redial's
  outcome, with the caller's code and when. Mechanism: the second ask is a Pending marked
  `again`; HostTransport dials in Links' two halves (connect, offer) so a dial that made no
  connection (refused, unreachable, TLS handshake or verification) ends every `again` call
  connection_aborted (fail_again), while a dial that lost the tie-break waits for the peer's
  link; fail() (remove_node, drain, stop) ends `again` calls connection_aborted; the posted
  retry is skipped (connection_aborted) when the host stops or drains or the node is unrouted.
  Class swept, one value test per row (rpc_host_test, in the fetch entry, each alone too):
  fetch-gone (peer process exits in its reader: connection_aborted in 1 ms of a 4 s deadline;
  ea48bc8: timed_out at 4000), fetch-unverified (reload trusting rogue.pem: redial fails
  verification, connection_aborted at 509 ms), fetch-removed (remove_node 206 ms, drain past
  300 ms drain_deadline 509 ms, both connection_aborted and not asked again; ea48bc8:
  not_connected, resource_unavailable_try_again), fetch-twice (connection_aborted at 2517 ms,
  reader twice), fetch-dead (the up row). Control: mutant/fetch-any-connection (consensus:d627e22,
  ea48bc8's host_transport.h with these tests).
- F2 (blocking, fixed): fetch-partial (the reviewer's probe-partial shape: 2 MiB+ of 48 MiB in
  when the link is held past dead_after; exact, reader ran twice). Control:
  mutant/fetch-no-clear (consensus:af9ba03).
- F3 (taken, small): fetch-oversize: a quiet 48 MiB over 32 MiB beside a 4 MiB: message_size,
  exact, links unchanged, failed 0, asked again 0; a run with a dead or stuck end says
  "inconclusive" and passes on the values alone. Control: mutant/fetch-oversize-aborts
  (consensus:326089a, the reviewer's mutant).
- F4 (fixed): drain and remove now end connection_aborted (the base's code) and never ask
  again, for symmetry with stop; host_transport.h's fetch comment and the table state both.

Cause (suite probe 45453078372, macos-15-intel shard 2, suite 1, 18:31:47; job 138655125896):
- Host 2's loop 0, which holds both its links to node 1 (loop_of_node(1) = 0), sat 5,221 ms in
  state U inside __sendto < BIO_write < SSL_write < Connection::flush: an uninterruptible kernel
  wait in a non-blocking write, sockets empty (sndq 0, windows open, rexmit 0). The sampler
  itself stalled 1437 -> 5476 ms behind the socket lock. Host 1 heard nothing on either link for
  its 5 s dead_after and ended both dead (dead 2 at 6378 ms, links still 2/2); host 2 then found
  them closed (failed 2); the redial made links 3+3. So the third link is the consequence, not a
  precondition. Every fetch on the Bulk link ended connection_aborted, the 48 MiB one included.
- Kernel side: 16 KB clusters 425 active of 436 created in that run, against 470-578 of 988 in
  the passing suites: the jumbo pool had to grow, and XNU's m_clalloc growth sleeps
  uninterruptibly (groups-upload-loop-stall's XNU reading). Heartbeats were not affected
  because the case's heartbeats had stopped (beats 14 from 1055 ms on).
- Same class as groups-upload-loop-stall (U-state write). Not the fetch-alongside stall: there
  the held loops ran the test's reader (pattern(8 MiB)) with the process stalled 2.6 s.
- Pairing probe 45449779597: 100 rounds of the rpc_host cases, 0 failures; the whole suite is
  needed to push the kernel there.

Fix (consensus:22dcba4, ea48bc8): a fetch is a read, so when its connection ends under it
(any path to Lane::close: dead, stuck, socket/TLS failure, abort for overrun/malformed/
drain_deadline/reload/remove, a new Bulk link replacing it, a protocol violation) HostTransport
asks once more on the next connection, same deadline and fetches slot; connection_aborted
reaches done only when both end; not when the host is stopping (not_connected/not_processed
paths unchanged). LaneStats::fetches_asked_again counts them. fetch.h and rpc-lane.md say so.
Test: rpc_host_test fetch case runs fetch-dead after it (and `fetch-dead` alone): host 2's
reader holds its connection loop 1.5 s against host 1's 1 s dead_after with held/huge/big on
the link: each asked again, 4 MiB exact, 48 MiB message_size. Mutant (local branch
mutant/fetch-not-asked-again, 536024a): all three end "Software caused connection abort",
exactly CI's line. Local: rpc_host cases 14/14.
Not fixed: the kernel's 5 s write. Option for the lead: cap TLS records below 16 KB
(SSL_CTX_set_max_send_fragment) so loopback sends use 4 KB clusters; unmeasured.

Queued: 261124-123054-1bea5f (A1 fails-without at 536024a), 261124-123059-eda87c (A1
fetch-dead x20 at ea48bc8), 261124-123104-84309d (A2 fetch x200 beside 4 burners), suite
261124-123112-1fc948; CI 45481678940 at ea48bc8; suite probe 45481730112 (probe branch rebased
on ea48bc8 at 1427d38, 2 x 120 min of suites on macos-15-intel).

msg-1: done: probe branch, CI reproduction (45453078372), cause named above, fixed in the file
set; counters land with the fix, not alone.

Probe results so far:
- 45449779597 (cd844d4, rpc_host cases under ctest --parallel 2, fetch beside stalled-peer as
  CI pairs them): shards 3 and 4 (macos-15-intel, 4 CPUs, 14 GB, nmbclusters 65536): 50 of 50
  passed, every loop's worst stall under 100 ms, links ended dead/stuck/failed 0. The pairing
  alone does not reproduce it; shards 1, 2 and arm64 still running.
- So the trigger needs the whole suite before it (CI's failures came ~20 min into a suite).
  45453078372 (0bc64d2): 2 macos-15-intel shards run whole suites for 120 min each; every
  fetch run prints loops' worst stalls, kernel counters over the run (mbuf waits, 16 KB
  cluster failures, free pages, pageins, decompressions) and its sockets; a failed run prints
  the 100 ms samples, vm_stat, netstat -m and a spindump if a loop sits in U.
- Prior art: groups-upload-loop-stall found a macos-15-intel connection loop in write(2) in
  state U (uninterruptible kernel wait, likely m_clalloc mbuf growth) for 0.3-4.6 s; the
  fetch-alongside stall (45372452058) sampled loops inside the test's fetch reader with the
  process stalled up to 2.6 s. Which one this is waits for a captured failure.
Next when resumed: read 45453078372's fetch lines (grep "^fetch: \|^probe: one's"), name the
rule and the held loop; then the fix and the A1/A2 CI loops at the fix and at main.

Lead's ruling (this session): reproduce on CI's runner. Probe branch probe/detent/fetch-stall
(consensus:cd844d4 = d659f76 + test/transport/fetch_probe.h, groups_test's LoopProbe/KernelProbe/
Spindump/sockets copied, + .github/workflows/probe-fetch.yml): every rpc_host case under ctest
--parallel 2, 4 x 25 rounds on macos-15-intel, 10 on macos-15; each fetch run prints link-end
counters and each loop's worst stall (state, stack); a failure prints 100 ms samples and a
spindump. Run 45449779597. Probe worktree: ~/.scratch/detent/consensus-fetch-oversize-abort/
probe-wt (delete it and the branch when done).

Lead question, same stall as consensus-fetch-alongside-loop-stall? Its run 45372452058 sampled
node 2's held loops 0 and 1 inside serve_fetch -> the test's reader (pattern(8 MiB)), with the
Lag sampler seeing the whole process stalled up to 2.6 s: a reader that allocates and fills
MiBs on a connection's loop, on a runner that stalls processes. Here the server's reader
allocates 48 MiB (huge) and copies 16 MiB (big) and 4 MiB (slow) on two's loop of node 1. To be
confirmed by the probe's stacks.

Cause hunt done as far as this host allows; diagnostics pushed (consensus:d659f76, CI on
quest/detent/consensus-fetch-oversize-abort).

Finding: the oversize path is not the cause. The Bulk link itself ends ~5 s into a stall, and
every call on it ends connection_aborted; the 48 MiB fetch was only the call in flight.
- CI 45384169136 (macos-x86_64): the 16 MiB fetch, under fetch_bytes, ended "Software caused
  connection abort" after 5995 ms; Control heartbeats kept a 194 ms gap; links 3+3 (Bulk
  redialed). No oversize involved.
- 45407143806 a2 (the quest's line): fetch case took 8.06 s vs 1.54 s passing
  (45415901609): ~5 s more, dead_after's 5 s. 45407143806 a1, 45374547236, 45425539843,
  45366654906 (main): the same stall recovering before the rule: heartbeat gaps 4976, 3746,
  3765, 1564 ms, message_size correct each time.
- Code read: the fetcher's refusal is its own Cancel (HostTransport::fetch body ->
  stop(message_size) -> Lane::stop, a Cancel and finish of that call only); the server drops
  that call's queued chunks; late frames are rule 9 (late), not violations. connection_aborted
  comes only from Lane::close via close_rpc: the Bulk link ended.
- Every failing run ran fetch beside rpc_host_stalled-peer (ctest --parallel 2), but so did
  passing ones; not decisive.
- Not reproduced here (x86_64 i9, so CI's arch): 40/40 plain; 48/48 at 6 at once beside 12
  burners (big fetch 3.7-6.6 s, as slow as CI); 20/20 beside stalled-peer; 15/15 rounds of the
  rpc_host cases under ctest --parallel 2 as CI runs them. Probes (connection.h PROBE prints:
  errno, stuck-rule values, reader/writer ends; ~/.scratch/detent/consensus-fetch-oversize-abort/
  probe.diff) saw no link end before teardown.
- consensus:d659f76 counts link ends by rule (HostTransportStats dead/stuck/failed;
  Connection::stuck) and the fetch line prints them, so the next macos-x86_64 failure names
  the rule (dead: Bulk silent 5 s; stuck: a write waited 5 s with no drain; failed: a
  socket/TLS error such as ENOBUFS, which socket_writes treats as fatal).

## Next

1. Read the jobs queued at consensus:87cb821 (Evidence) and CI 45507969326; then the review's
   second look.

## Blocked on

Nothing.

## Evidence

At consensus:87cb821 (the review round; local: rpc_host ctest 14/14, each mutant 3/3 failing
for its stated reason):

| Criterion | Evidence |
|---|---|
| A1 | Cause: suite probe run 45453078372 (Doing). Tests at the fix, 10 runs each: fetch-partial 261124-151246-f0ad2c, fetch-twice 261124-151248-f49548, fetch-gone 261124-151250-cd4e3b, fetch-unverified 261124-151252-3b3f74, fetch-removed 261124-151254-e42706, fetch-oversize 261124-151256-f001a3; fetch-dead x20 261124-151308-fdd296. Fails without: fetch-gone 261124-151258-073436, fetch-unverified 261124-151300-44e305, fetch-removed 261124-151302-d24b61 (mutant/fetch-any-connection d627e22, ea48bc8's host_transport.h); fetch-partial 261124-151304-e49b9a (no-clear af9ba03); fetch-oversize 261124-151306-086ff4 (oversize-aborts 326089a). Earlier: fetch-dead fails without asking again 261124-123054-1bea5f |
| A2 | 261124-123104-84309d (fetch x200 beside 4 burners, light, at ea48bc8; not rerun at 87cb821, which changes the redial path: the lead decides) |
| A3 | [artifact] docs/mechanisms/rpc-lane.md: the fetch bullet and "A fetch whose connection ends" (two tables: each way a connection ends x the redial's outcome, the caller's code and time), consensus:afec3fe, 87cb821 |
| A4 | suite 261124-151310-7f0e32 (45 min) and CI 45507969326 at 87cb821, pending; at ea48bc8: suite 261124-134701-6d9c49 171/171, CI 45481678940 green |

Other runs examined: pairing probe 45449779597 (100 rounds, 0 failures); suite probe 45481730112
at 1427d38 (fix + probe) pending.

## Result

A fetch whose connection ends under it is asked once more on the next connection
(consensus:22dcba4, ea48bc8; d659f76 counts link ends dead/stuck/failed). Cause and evidence in
Doing; docs/mechanisms/rpc-lane.md. Open: the macOS kernel's 5 s uninterruptible sendto itself
(groups-upload-loop-stall's class); capping TLS records below 16 KB is an unmeasured option.
Cleanup after landing: the probe branch probe/detent/fetch-stall and its worktree
~/.scratch/detent/consensus-fetch-oversize-abort/probe-wt, the worktree mutant-wt beside it and the local branches mutant/fetch-any-connection, mutant/fetch-no-clear, mutant/fetch-oversize-aborts, mutant/fetch-no-again-map and
mutant/fetch-not-asked-again.

## Rulings I made

- F1: a redial whose hellos fail or whose peer refuses this node's boot or wire version is not a
  connection failure: the first is redialed after `redial` (200 ms) and then fails to connect;
  the second redials to fetch_deadline (timed_out), since the peer is up and answering. The
  table states both. Telling them apart would need Links::offer's reason, outside the ruling.
- F1: HostTransport composes Links::connect and Links::offer instead of changing Links::dial's
  bool (links_test and admission_nodes_test, outside the file set, use it).
- F4: drain and remove_node end a fetch connection_aborted and never ask again (as stop does),
  rather than documenting resource_unavailable_try_again and not_connected: the connection
  ended and no next one took the fetch, which is what connection_aborted says.
- fetch-unverified reloads with rogue.pem as the CA bundle: rpc_host's fixture has no second
  CA, and the reload keeps node 1's own key, as reload requires.

- The fix is at the fetch, not the dead rule: dead_after correctly ends a link whose peer is
  silent 5 s; a read survives it by asking once more. Once, not until the deadline, so a peer
  that kills every link costs at most two reads.
- fetch-dead rides the fetch ctest entry (CMakeLists.txt is outside the file set); `fetch-dead`
  runs alone for its control.
- Diagnostic counters instead of a fix: no cause to fix is evidenced yet, and the oversize path
  is sound; HostTransportStats gains dead/stuck/failed (link ends, not a contract change).
- ConnectionEnd unchanged (connection_test asserts a stuck end is `failed`); Connection::stuck()
  tells the stuck rule's end apart.
- host-transport.md is outside the file set: the counters are documented in the header and in
  rpc-lane.md's fetch row.
