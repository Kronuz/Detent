# consensus-transport: async-drivers 5f: the rpc lane on Bulk, fetch as lane method 1, process stops end to end

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-transport`.

## Outcome

The request/response lane (rpc-lane-codec) runs on the host transport's Bulk connections (transport-groups, transport-identity), serving Xapiand's consensus-rpc-lane (consensus:0fd8a7f docs/design/rpc-lane.md revision 2, approved at Xapiand:a3b4b4903, T1-T8), and fetch is lane method 1. Split by D106: transport-connection, transport-groups, transport-identity, transport-idle-cost and rpc-lane-codec carry the rest of the former A1-A7; this id stays the last step, so Xapiand's orderings after it hold.

## Acceptance criteria

- A1 Bulk routes by the first 8 bytes, small frames FIFO, chunk classes alternating outside Bulk's connection window (T2); `Transport::hold(node)`/`release(node)` keep a Bulk connection to a node outside every group (T3); lane-only connections and `lane_idle` (D7): tests
- A2 The reader hands each lane frame over synchronously from a per-connection buffer, never waiting on a shared budget (T5); call deadlines use transport-connection's late-deadline rule (T7); a graceful close sends the flushed frames, then `Drain`, then close_notify (T8), so a call racing a close never ends with an unknown outcome: tests, a mutant for T5 killed
- A3 Queued outgoing chunks are capped per connection (R2, Xapiand:a3b4b4903), so a slow peer's Bulk socket can't stall writes to every other peer: a test with one stalled peer and one live peer, and a mutant without the cap killed
- A4 Fetch is lane method 1 (D8) and shares the bulk budget without delaying consensus frames (Xapiand F5): a test with a fetch beside a maximal upload against heartbeats
- A5 Process stops (D102): fetch-alongside-consensus, ported onto the lane's fetch, passes on macOS x86_64 CI three runs in a row with no skip, and a placed SIGSTOP inside a drain proves a late reply drains over the same lanes (F18: a never-drain mutant killed); timing scenarios carry the ctest label `timing`, off CI (D100). `Peers` and DTFT0001 stay until worker-pump-removal deletes them, so consensus main stays a valid Detent pin (D106): tests, CI runs
- A6 rpc-lane-codec's review notes (Detent reviews/rpc-lane-codec-security.md) close before the lane is wired: (N3) a `ready` callback that closes the lane is no use-after-free (ASan case, rpc.h:703 at consensus:8db3ce3); (N2) `send` past a call's known total is refused at once instead of stalling to the deadline; (N1) the unsent queue a never-reading peer builds is charged to `rpc_outgoing_bytes` and bounded by A3's per-connection cap; (N4) the fuzzer reaches pool waits and rule 7.6: tests, an ASan job, a fuzz job

## Contracts

Provides `consensus:rpc-lane` on Bulk and fetch as lane method 1. Consumes transport-groups, transport-identity and rpc-lane-codec.

## Decisions

D96, D97, D102, D106. Xapiand cluster-release (Xapiand:15c3e05ac) and consensus-rpc-lane order after this quest by this id.

## May land early

Tests; the transport is additive beside Peers.

## Notes

Re-planned 2026-11-19 from the untracked-commitment audit (U1): dropped 2026-11-17 (Detent:7487ddec) to re-plan once its split quests landed, and they have. On consensus main `rpc.h` is included by no production header and `host_transport.h` has no lane. A6's N2 is verified open: `rpc.h:386` `send` accepts bytes past a known total. consensus-fetch-macos's F13, F16 and F19 fold in here (D102). consensus-quiescence and detent-on-drivers wait on it, and Xapiand's consensus-rpc-lane orders after `detent:consensus-transport`.
