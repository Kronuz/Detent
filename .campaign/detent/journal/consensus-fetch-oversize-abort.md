# consensus-fetch-oversize-abort: journal

State: waiting on jobs CI 45449779597 (pairing probe), 45453078372 (suite probe), 45448063559 (quest branch)
Quest: .campaign/detent/quests/consensus-fetch-oversize-abort.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

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

1. Lead's ruling on Blocked on.
2. Read the next macos-x86_64 failure of rpc_host_fetch (this branch's CI or any branch with
   d659f76): its "ended dead/stuck/failed" names the rule; then fix the stall it shows.

## Blocked on

Lead: the quest's premise (the oversize refusal costs the link) does not hold; the cause is a
Bulk-connection stall on macos-x86_64 CI that a dead_after rule (5 s) then ends, the same stall
as the heartbeat-gap failures. It does not reproduce on this host. Options:
1. Recommended: keep this quest open on the diagnostic (d659f76, land it: small, no contract
   change) and wait for the next CI failure to name the rule; then fix the stall here.
2. Fold it into consensus-fetch-alongside-loop-stall (heartbeat gaps under Bulk on macOS),
   which shares the stall, and close this one after d659f76 lands.
3. Quarantine rpc_host_fetch on macos-x86_64 (CMake label; outside this file set) while 1 or 2
   runs, to unblock the three landings. A mask, not a fix.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Diagnostic counters instead of a fix: no cause to fix is evidenced yet, and the oversize path
  is sound; HostTransportStats gains dead/stuck/failed (link ends, not a contract change).
- ConnectionEnd unchanged (connection_test asserts a stuck end is `failed`); Connection::stuck()
  tells the stuck rule's end apart.
- host-transport.md is outside the file set: the counters are documented in the header and in
  rpc-lane.md's fetch row.
