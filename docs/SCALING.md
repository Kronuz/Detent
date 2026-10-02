# Scaling Detent's control and distribution planes

**Author:** [Germán Méndez Bravo](mailto:german.mb@gmail.com)  
**Date:** 2026-10-02  
**Status:** Preliminary scaling plan; implementation is gated by the [Validated Architectural Blueprint](VALIDATED_ARCHITECTURE.md)

The high-effort architecture review supersedes this document wherever their guidance differs. It conditionally approves the topology, rejects adoption of the current journal prototype, and requires consensus safety, recovery, and compatibility gates before implementation proceeds. The capacity targets below remain unvalidated assumptions.

## Abstract

Separate the durable write authority from regional distribution and host-local evaluation. Extend the Kronuz libraries with a durable consensus module, a bounded binary transport, and reusable journal/snapshot primitives. Preserve Xapiand's existing public interfaces and multicast wire format while adding strict durable capabilities through separate interfaces. Correct unsafe legacy consensus behavior in separately validated changes, including mixed-version compatibility checks.

The planning workload is 500,000 knobs, 300 concurrent publishers, 1,000 transactions per second, and 50,000 agents. These are test targets, not demonstrated capacity. Namespace subscriptions and delta distribution are required: changing the encoding alone cannot remove fleet-wide delivery cost.

```text
Publishers -> bounded ingestion -> 3/5 durable Raft voters
                                      |
                              committed change stream
                                      |
                           regional distribution relays
                                      |
                           namespace-subscribing agents
                                      |
                          immutable local binary images
                                      |
                         apps: mmap lookup, no network
```

## Motivation and capacity model

The current distributor serializes and persists a full snapshot for every write, copies the whole map when applying a PATCH, derives the delta by comparing complete states, and serves fleet-wide polls. The agent has a fixed 4 MiB mutable mapping, and its seqlock does not remove the C++ payload data race. JSON inputs default to 100,000 knobs and 16 MiB. The current Raft implementation has volatile terms, votes, and logs; a restart silence interval is not durable consensus.

| Workload assumption | Consequence |
| --- | --- |
| 500,000 knobs × 128 encoded bytes | About 64 MB of logical state; group overrides can raise this substantially |
| 50,000 agents each cold-starting with 64 MB | 3.2 TB of aggregate transfer before compression |
| 1,000 transactions/s × 2 changed knobs × 128 bytes | 256 KB/s of raw logical changes |
| Every agent receiving every change | 12.8 GB/s aggregate delivery, excluding framing and replication |
| 100 relays × 500 agents | About 128 MB/s per relay in the all-knobs case |
| Each agent subscribing to 10% of state/changes | About 90% less logical data delivered to that agent, assuming uniform traffic |

The byte assumptions are illustrative. The benchmark corpus and measured sizes decide actual capacity. Batching reduces messages and repeated changes to the same knob; it does not reduce the bytes of distinct updates that every subscriber must receive. Regional relays distribute egress and recovery load; they do not make aggregate bandwidth disappear.

Initial performance goals are p99 local library lookup below 10 µs and p99 steady-state regional propagation below 250 ms. CLI process startup is measured separately. These goals require representative hardware, payloads, and failure testing before they become an SLO.

## Write semantics and admission

Publishers submit bounded, atomic transactions containing knob upserts/deletions, a client request ID, and optional expected per-knob revisions. The authority assigns committed revisions. Independent publishers must not allocate a shared global version themselves. Compare-and-set prevents a stale edit from silently replacing a newer definition; unconditional edits remain an explicit policy.

Idempotency belongs in replicated state. Store the client/request ID, request digest, and result together with the mutation. Repeating an ID with a different digest is a conflict. Define a retention window and report an expired result as unknown; a transport timeout must never assert that an update was not committed. Reject oversized requests before buffering them. Bound queued bytes as well as request count, with per-publisher budgets and explicit retryable overload responses.

Start with one consensus group and fixed membership of three or five voters. Hundreds of publishers are clients, not voters. Agents and relays do not vote. Namespace ownership is the partition boundary when write load eventually requires multiple groups. Cross-group atomic transactions are excluded from that partitioned mode until a transaction coordinator exists; requests must fail explicitly rather than appear partially atomic.

The `/v1/snapshot` JSON endpoint remains useful for bootstrap and administration. It cannot provide safe independent concurrent publication using client-assigned versions alone. A versioned transaction API supplies server-assigned revisions and expected-value semantics without changing the meaning of the existing endpoint.

## Durable consensus and Xapiand compatibility

Keep `cluster::Bus`, `cluster::Raft`, the existing delegate, and message types intact for Xapiand. Add a distinct durable-consensus module with explicit storage and transport interfaces. It uses stable peer IDs and fixed configured membership instead of discovery-derived quorum. Legacy and durable modes use different, versioned frames and must not form mixed voting groups.

Persist term/vote before granting a vote or emitting a higher-term message. Persist appended entries and conflict truncations before acknowledging replication. A leader acknowledges a publisher only after a durable majority commits the operation and the deterministic state machine records its result. A storage error fences the member; continuing to vote after a failed durability barrier is unsafe. Save committed progress and checkpoint metadata so restart can restore state and replay safely.

The consensus algorithm needs one vote per term, up-to-date-log voting, independently tracked follower progress, majority commitment restricted to the current term, and an initial current-term no-op after election. A slow follower must not stall a healthy majority. Bound proposed commands and outstanding bytes per follower. Membership changes remain disabled until joint consensus and its tests exist.

Use point-to-point, length-delimited TCP messages for durable replication, including bounded AppendEntries batches and chunked snapshot transfer. UDP multicast remains the discovery/legacy transport; a multi-megabyte state is not a datagram. Run protocol state on one owning executor and make disk completion an explicit phase. Do not block a network reactor on fsync. Storage completion must precede the protocol responses that depend on it.

Compatibility gates:

1. Run existing cluster election, relinquish, liveness, and wire tests unchanged.
2. Add deterministic durable-consensus tests for partitions, duplicate/reordered messages, divergent logs, restarts, and failed writes/syncs.
3. Run Xapiand's cluster harness against the same local library revision. Its old wire bytes and leader/discovery behavior must remain unchanged.
4. Pin a tested library revision in Detent. Upgrade Xapiand separately; do not make Detent's local FetchContent override a release requirement.

The persistence obligations follow the [Raft paper](https://raft.github.io/raft.pdf). Adding disk writes to the legacy implementation without fixing its voting and recovery semantics would not establish those obligations.

## Persistence and state application

The journal stores opaque checksummed records and durable commit/checkpoint boundaries. It does not know knob names, HTTP, or Raft node types. The consensus storage adapter supplies terms, indexes, votes, suffix replacement, and checkpoint metadata. Group commit combines durability barriers while preserving which replies are allowed after each barrier.

Persist mutations rather than rewriting a full JSON document on each transaction. Apply commands incrementally on the state machine's owning executor and export the applied delta directly. Avoid full-map cloning and full-state diffing in that path. Snapshot generation runs separately from ordinary ingestion and receives a consistent committed view; it must not serialize mutable state concurrently without synchronization.

Checkpoint files contain the last included index/term, configuration identity, state checksum, and idempotency metadata. Write and sync the new checkpoint, publish its manifest atomically, and sync the containing directory before reclaiming covered journal segments. Recovery accepts complete verified records, handles a torn final record according to the durable boundary, and refuses corruption in acknowledged history. Never treat a corrupt durable log as an empty database.

Evaluate the existing [Kronuz/storage](https://github.com/Kronuz/storage) volume engine for opaque checkpoint blobs. A generic journal needs separately tested recovery and suffix-replacement semantics; append-only blob storage alone is not a Raft log. Keep these modules distinct inside a reusable storage boundary instead of importing application state into the library.

## Distribution and binary encoding

Serve a bounded, resumable change stream identified by an epoch and committed revision. A client presents its cursor and namespace subscription. Retain history by bytes and time; a cursor behind retention receives an explicit reset instruction and a consistent checkpoint, followed by changes after that checkpoint. Reconnects use randomized backoff and independently bounded snapshot admission to prevent fleet restarts from exhausting the authority.

Relays subscribe upstream once and fan out locally. They retain committed changes and compiled checkpoints, and never acknowledge publisher writes. Their subscriber buffers have byte limits. A slow subscriber is disconnected with a resume/reset path rather than retaining an unbounded private history. Agents retain last-known-good state during outages and report connectivity separately from local readability.

Use a versioned binary schema for snapshots, transactions, and deltas. Specify byte order, integer ranges, record lengths, deletion markers, and unknown-field behavior; do not expose native C++ structs as a network protocol. Validate sizes before allocation, bound decompressed output, and distinguish corruption detection from authentication. Benchmark compact binary encoding against JSON plus zstd using the same corpus; compressed size and encode/decode CPU both matter.

Namespace subscriptions reduce unrelated data. A transaction is published atomically to each relevant subscriber. Distribution may coalesce sequential committed transactions into a later complete state because knobs are latest-state configuration, but the authority preserves their audit history and results. Snapshot/delta handoff must carry exact base and target revisions; a mismatched base triggers resynchronization.

TLS and peer authentication are part of production transport, with publisher authorization at namespace boundaries. A shared multicast token is neither authorization nor encrypted transport. Limit tenants independently so one publisher cannot consume every queue or checkpoint slot.

## Host-local publication

First replace the fixed mutable mapping with size-bounded immutable generations, atomically published after complete construction. Readers pin a generation; old mappings remain valid after pathname replacement. A small stable control mapping can announce generation changes and source health using lock-free atomics, leaving payload bytes immutable. This addresses both larger snapshots and the payload race.

For sustained large-state updates, compile independent immutable pages and publish a manifest so a small transaction rewrites affected pages rather than the entire image. Readers use a process-local cache of pinned mappings and refresh only when the generation changes. Multi-key reads pin one manifest for a consistent transaction view. Reclamation must account for lazy page access and active readers; a missing old page requires retrying the entire view, never mixing generations.

Persist agent checkpoints in the user/system state directory when offline restart recovery is enabled. Runtime mappings are disposable and may vanish on reboot. These are separate lifetimes. Local lookup does not consult the network or wait for consensus.

## Modules and implementation sequence

| Module | Reusable mechanism | Domain glue |
| --- | --- | --- |
| `cluster` durable consensus | Elections, replication, commit/recovery, bounded proposal admission | Detent mutation application |
| Journal/checkpoint storage | Checksummed records, barriers, recovery, segment reclamation | Raft metadata adapter and knob checkpoint encoding |
| Binary framing/streams | Bounds, version negotiation, transport flow control | Knob schema and namespace authorization |
| Immutable publication | Generation control, mapping lifetime, atomic publication | Knob lookup and rollout evaluation |
| Change history | Byte/time retention, cursor/reset semantics | Namespace subscriptions and transaction deltas |

Land and validate each step independently:

1. Align daemon execution/status and service templates. **Implemented.**
2. Add the 100,000/500,000-knob baseline and concurrent ingestion/fan-out harnesses. **State-cost baseline implemented; concurrent harnesses remain.**
3. Add reusable durable journal/storage interfaces and strict consensus alongside the untouched legacy module; prove crash and partition behavior before selecting it in Detent.
4. Add server-assigned transactions and incremental state application, with persisted idempotency and bounded admission.
5. Add portable binary snapshots/deltas, immutable dynamic agent publication, and namespace subscriptions.
6. Add resumable asynchronous distribution and relays, then page-level local publication and persistent agent recovery.
7. Measure mixed ingestion/fan-out, overload, cold starts, slow clients, quorum loss, disk errors, and process kills. Increase agents through 1,000, 10,000, and 50,000 using enough hosts and connection/file-descriptor limits. A synthetic model or single-host connection test does not certify fleet capacity.

## Alternatives considered

1. **etcd as the write authority.** Provides mature durable consensus and revision watches. Rejected for this implementation: the chosen direction is to improve the Kronuz consensus/storage stack and keep its mechanisms reusable.
2. **Larger JSON snapshots with the current write path.** Raises the capacity ceiling but leaves per-update CPU, disk amplification, and fleet recovery costs tied to the complete state size.
3. **One Raft cluster containing agents.** Couples availability to the fleet and multiplies consensus traffic. Keep the voting group small and distribute committed state through non-voting relays.
4. **Binary encoding alone.** Can reduce parsing and framing costs, but does not solve durability, concurrent publication, or all-subscriber bandwidth.

## Experimental results

The [scale benchmark](../benchmarks/detent_scale_bench.cc) uses 100 namespaces, equal boolean/integer counts, and two group overrides per boolean. Release build on macOS x86_64 with Apple Clang 17.0.0. These are synthetic inputs; no production knob corpus was supplied. Timings are medians of five repetitions at 100,000 knobs and three at 500,000. Each timed operation includes destruction of its temporary result.

| Knobs | JSON bytes | Flat image bytes | JSON encode | JSON parse | Apply one-knob PATCH | Flat image encode |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 100,000 | 11,123,377 | 9,201,397 | 195.872 ms | 296.130 ms | 148.624 ms | 19.778 ms |
| 500,000 | 56,283,377 | 46,381,381 | 992.572 ms | 1,708.244 ms | 780.557 ms | 108.445 ms |

The single-knob patch is 139 JSON bytes in both cases. Its state application still copies the complete map. Neither image fits the current 4 MiB mapping, and the larger JSON exceeds both default JSON limits. Flat image encoding here is the native local layout, not yet a portable network codec. These numbers establish bottlenecks; they do not measure concurrent publisher throughput, durable commit latency, or fleet fan-out. [Raw results](../benchmarks/scaling-baseline.json) preserve the corpus and build details.

```sh
cmake --build build --target detent_scale_bench
./build/benchmarks/detent_scale_bench 100000 5
./build/benchmarks/detent_scale_bench 500000 3
```

## Evidence and completion gates

Native correctness, real-process tests, and reproducible benchmarks are required for each landed module. Record corpus shape, hardware, build flags, queue limits, persistence mode, and compression settings with every result. Keep successful write acknowledgments traceable through kill/restart tests. Report scale limits and incomplete capabilities plainly; the existing Raft chaos tests do not certify durable consensus.
