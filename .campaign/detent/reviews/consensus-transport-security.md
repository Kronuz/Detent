# consensus-transport review: security

Commit: consensus:34881c299ce0a810beb8fe19b8b508fc08d5713f
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This review also carries the correctness lens. The attacker is an authenticated peer on the other
end of a Bulk connection: buggy, stalled or compromised, as in rpc-lane-codec's review. It can
send any frame sequence and read its socket as slowly as it likes.

## Evidence I relied on

All at consensus:34881c2, each exit 0:

- 261120-224221-77bfd0: the suite, 63 of 63.
- 261121-022848-250e05: rpc_host's 16 cases, `stalled-peer` included (A3), plus rpc_test's
  `overrun` and `unread-queue`.
- 261121-022848-b6c257: rpc_test under ASan, `reenter` included (N3).
- 261121-022848-7ccc3f: a 20-minute fuzz at `-max_len=65600` with 0 crashes, then coverage.
- 261121-022849-49400e: the corpus coverage.
- 261121-022849-bfd9fe: fetch-alongside-consensus and stop-drain, 10 runs each.
- CI run 44876805517 at 34881c2: every job green. `macos-x86_64` succeeded in attempts 1, 2 and 3,
  which I checked with `gh api .../attempts/N/jobs` (A5).
- The fails-without jobs in the journal's Evidence table, by their records. I did not rerun them.
- The decoder is unchanged since consensus:8db3ce3. Only constants differ in `rpc.h`'s first 300
  lines. So rpc-lane-codec's oracle job 261117-055231-96b757 still stands: 20M frames, 0
  mismatches.

## What I checked myself

My probes are in `.scratch/detent/review-consensus-transport-security/` in the home checkout:
`probe.cc` and `run_probe.sh`, built with ASan and UBSan through `cmake/`. Rerun them with `sh
run_probe.sh probe.cc` in a consensus checkout. Each probe drives the real `rpc::Lane` with the
host's default pools: 256 MiB serving, 256 MiB calling and 64 MiB outgoing. The peer never calls
`next()`.

Job 261121-044113-5001aa:

- `refusals`: Calls to an unknown method. The 257th throws `ProtocolError`. The N1 fix holds for
  refusals.
- `errors`: Calls to a served method whose handler ends the call with `stop(Code(256))`. That is
  `serve_fetch`'s `fetch_missing` path (`host_transport.h:1454-1455`). 3,000,000 Calls are never
  closed. They leave 72,000,000 bytes unsent against a 262,336-byte `queue_bytes`, with
  `rpc_outgoing_bytes` at 72,000,000 against its 64 MiB limit. A second connection's call on the
  same pools then cannot queue its `Call`. The probe's RSS was 978 MB.
- `credits`: Calls with a body to come, which queue a `Credit` at accept, each then cancelled by
  the peer. 3,000,000 Calls leave 96,000,000 bytes unsent and are never closed. Again a second
  connection's call is starved.
- `deadline`: Calls whose handler never answers, with `timeout_ms` 1. `expire()` answers each
  with an `Error`. 1,024,000 Calls leave 24,576,000 bytes unsent and are never closed.
- Job 261121-043855-dd5f4d ran the same probes at 2M Calls: 48 and 64 MB unsent, still under the
  limit.

By reading:

- **The host's reader** passes every lane frame to `Lane::receive` with no check on the lane's
  queue (`host_transport.h:1486-1500`).
- **The writer's stuck rule** is the only stop for a peer that does not read
  (`connection.h:226-240`). It ends a connection whose write wait reaches `dead_after` (5 s) with
  nothing acknowledged. A peer that acknowledges a little every few seconds is "a slow reader,
  kept".
- **Lane lifetime.** `close_rpc` destroys a Lane only from `ended()`, from `open_rpc()` and after
  `receive` has returned (`:1095`, `:1346`, `:1496`). `Connection::close` and `abort` are
  asynchronous (`connection.h:133-165`), so no Lane callback can destroy the Lane under its own
  frame. `RpcPeer` lives in a `std::map` (`:822`), so a callback that adds a peer leaves the
  outer references valid.
- **Fetch completions.** Every path ends a fetch's `done` exactly once: `close()`, `expire()`,
  `fail()` for pending calls and `stop()`. `fetching` is decremented in `done`. stop-drain covers
  the timeout and the late reply.
- **The sender's and receiver's window bounds** both count whole frames, header included: the
  sender at `:853` and `:936`, the receiver at `:1152`, and credit as `frame_header + payload` in
  `deliver`.

## Answers to the lead's questions

1. **Memory and starvation.** A3's cap holds for `Call` and `Body` frames. A response waits for
   room and keeps its slot, and a refused `Call` is closed past `serving`. Small frames, though,
   are charged past every limit, and only refusals are counted (F1). A peer that keeps sending
   and stops reading grows the queue without bound. Past 64 MiB it starves lane calls to every
   other peer. Consensus is not starved: Raft messages travel on Control (`send_message`), and
   the lane uses Bulk only. Served fetch replies are held outside every budget (F2).
2. **Decoder.** The decoder is unchanged since the oracle run. Group frames are length-checked
   before every read. In `arrived` a lane frame is told apart by `DTRP`; the full 8-byte magic
   and version are then checked by `decode`. A lane frame on Control, or a malformed one, aborts
   the connection. N3 is closed by the `Busy` depth guard, which ASan b6c257 confirms. N2 is
   closed (`rpc.h:410`). The fuzzer reaches the pool waits (ticket 512, granted 362) and rule 7.6
   (1). It never models a peer that does not read (F4).
3. **Ordering.** Small frames are strict FIFO, and chunks go round-robin by call. In `feed`, lane
   chunks alternate with groups' data through `lane_turn`. Lane frames never ride Control, so a
   peer cannot reorder or delay consensus messages. A small-frame flood is served first on its
   own Bulk connection only, so it delays that peer's own snapshot frames.
4. **Lifecycle.** I found no use-after-free and no lost completion (see "By reading" above). A5's
   ten runs and three x86_64 passes stand.
5. **Headers in budgets.** This is a sizing issue only, not something a peer can exploit (F5).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | **A peer that keeps sending and stops reading grows the lane's unsent queue without bound, and past `rpc_outgoing_bytes` it starves lane calls to every other peer.** The cause is in `small()` (`rpc.h:861-869`): it charges past the limit with `ByteBudget::charge`, and `receive` bounds only refusals (`:556`). Three kinds of frame are neither refusals nor tied to a live call: an `Error` from a served call's `stop` (`fetch_missing`, `handler_failed` and `message_size` in `serve_fetch`), an `Error` from `expire()`, and a `Credit` whose call the peer then cancels. Each frees its `serving` slot at once. This contradicts A6 N1 ("bounded by A3's per-connection cap"), journal R1 ("one per live call except refusals") and rpc-lane.md's "a table queues at most `queue_bytes` unsent, small frames included". Memory is about 330 B of RSS per 24-byte charged frame. The writer's stuck rule bounds a peer that reads nothing to `dead_after` (5 s) of its send rate, but not a peer that drips acknowledgments | probe `errors`, `credits`, `deadline`, job 261121-044113-5001aa | Bound every unsent small frame, not only refusals. For example, close with `ProtocolError` once unsent small frames pass `serving` plus the live calls, or count every small frame whose call has ended as a refusal. Add rpc_test cases for the three paths: a peer that never reads ends in `ProtocolError`, with `queued()` and `outgoing.used()` within the bound. Add a mutant that drops the check. On the fix, my probe should print `ProtocolError` for each path and "another connection's call queued its Call: yes" |
| F2 | no | gap | **Served fetch replies are held whole, outside every budget.** `serve_fetch` reads the whole file into `State::bytes` (`host_transport.h:1450-1461`), and holds it until the call ends. Only the frames are charged, as they queue. So a peer with 256 fetches in flight for a 64 MiB name holds `serving` × `fetch_bytes` = 16 GiB of this host, for up to the `timeout_ms` the peer chose (clamped to 1 h). A file larger than `fetch_bytes` is also read in full before the size check. An honest caller is bounded by its own `fetches` (4). The reader running on the loop is fetch.h's documented contract | `host_transport.h:1423-1466` | Owner's call: (a) charge the reply's size to `rpc_serving_bytes` (`try_acquire`) and answer `overloaded` when it does not fit; (b) cap the fetch replies a lane serves at once; (c) keep it and state the bound in rpc-lane.md's Limits. I recommend (a), with a test of 256 large fetch Calls from a peer that does not read |
| F3 | no | gap | The receive bound is per group, and the peer chooses the group ids. So frames sprayed across many ids, unknown ones included, are bounded only by how often their group loops run (`host_transport.h:1143-1157`). A stalled group loop lets the posted batches grow | reading | Optional: a per-connection cap on what `arriving` holds in total, or record the per-group scope in host-transport.md |
| F4 | no | gap | The fuzzer drains `next()` before every op (`fuzz/rpc_fuzz.cc:92`), so it cannot model a peer that does not read. The 20-minute run reached the refusal close 0 times, an outgoing wait holding room (`reserved_`) 0 times, and `roomless` 17 times | job 261121-022848-7ccc3f coverage | Add an op that skips the drain, so F1's bound and the `reserved_` path are fuzzed |
| F5 | no | nit | The headers in budgets (consensus-transport-budgets) are a sizing issue only. The sender and the receiver both count whole frames (`:853`, `:936`, `:1152`), and the receiver releases what the sender is charged, so an honest sender never trips `overrun`, and a peer gains nothing. One adjacent hazard: each end checks against its own `options_.window`, so two hosts configured with different windows abort honest connections as `overrun` | reading | Fold it into consensus-transport-budgets |
| F6 | no | nit | `~Lane()` calls `close()`, which defers when `depth_` is nonzero. A Lane destroyed inside its own callback would therefore free its memory without ending its calls, so their `done` would never run. Nothing in the host does this today (see "Lane lifetime" above) | `rpc.h:363`, `:474-478` | `assert(!depth_)` in the destructor |

## Not run

- No end-to-end host probe of F1 over TLS. The Lane probe drives the same `receive` and `stop`
  calls the host makes, cited by line, and the stuck-rule bound comes from reading.
- No probe of F2's memory, F3's spray, or windows that differ between hosts.
- No TSan or ASan run of my own, and no Linux run: CI and job b6c257 cover those.
- No fuzzing of my own.
- I did not rerun the fails-without jobs.
