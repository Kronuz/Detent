# Validated Architectural Blueprint

**Date:** 2026-10-02

**Status:** Conditional architecture approval. Implementation paused.

**Review:** Lead Software Architect pass, frontier `gpt-6-astra`, reasoning effort `xhigh`.

**Validation scope:** Source inspection and primary-source research. No implementation files were executed, no tests or builds were run, and no implementation files were changed during this pass. “Validated” identifies the reviewed architectural direction; it does not certify consensus correctness, durability, or fleet capacity.

| Reviewer | Status | Date |
| --- | --- | --- |
| Lead Software Architect sub-agent | Four-dimension source review completed; conditional approval with gates below | 2026-10-02 |
| Production qualification | Pending implementation, fault testing, and representative measurements | Pending |

## Abstract

The recorded 500,000-knob baseline takes 780.557 ms to apply a one-knob patch and 108.445 ms to rebuild the local image. The proposed authority → relay → agent topology addresses those whole-state costs, but adoption of the current durability prototype is **NO-GO**. Source review also found concrete safety defects in existing Raft behavior. Adding persistence around that behavior would preserve unsafe decisions.

The architecture direction receives **CONDITIONAL GO**: a small durable authority group, bounded transactions and incremental application, resumable relay distribution, and immutable local publication. Each implementation stage must establish its own correctness and capacity evidence before adoption.

```
Publishers → bounded transaction API → 3/5 durable authority voters
                                            |
                                 committed state + changes
                                            |
                                regional non-voting relays
                                            |
                                  subscribing local agents
                                            |
                                 immutable mapped views → applications
```

## Reviewed state and evidence

| Repository | Reviewed revision | Working-tree distinction |
| --- | --- | --- |
| Detent | `25ff26ffb205f037682dee72c5407723afe5ba02` | Clean before this documentation deliverable; includes the scaling baseline and preceding CLI changes |
| Sibling `Kronuz/cluster` | `36751be200dd34dc14a54c439bfc970de075e2f3` | Untracked `consensus/codec.h` and `consensus/journal.h` inspected separately as untested prototypes; they are not part of this revision |
| Sibling `Kronuz/storage` | `076add19576588991d6060cc77c297fe613a40a0` | Clean; read-only inspection |
| Sibling `Kronuz/Xapiand` | `b7e673ab601ffc75ad09d4ca5e532ba583e2151f` | Unrelated untracked `TODO.md` left untouched; source inspection only |
| Sibling `Kronuz/flume` | `7658ea9374d85b236a6362002048b80334d46577` | Clean; read-only inspection |
| Sibling `Kronuz/http` | `31a037c9679d4be0807176834c901a5bc8636335` | Clean; read-only inspection |

Detent links below are repository-relative. Links beginning `../../Kronuz/` identify external source in sibling checkouts under `~/code/Kronuz/`; the revisions above fix the reviewed source state. Untracked prototype links identify local review inputs and carry no committed-source guarantee.

The evidence categories used here are source-verified behavior, previously recorded measurements, analytical estimates, proposed mechanisms, and pending validation gates. Source-level counterexamples below were reasoned through without executing them. The committed [scaling plan](SCALING.md) and [raw baseline](../benchmarks/scaling-baseline.json) remain useful inputs; this review supplies the additional contracts and adoption gates.

## 1. Scalability Check

The 500,000-knob, 300-publisher, 1,000-transaction/s, 50,000-agent workload is a provisional design envelope. Those numbers remain assumptions. Publisher count alone says little about write capacity; transaction size, update rate, hot keys, subscription overlap, and burst duration decide it.

The committed [baseline results](../benchmarks/scaling-baseline.json) establish whole-state costs, not service throughput:

| Recorded measurement | 100,000 knobs | 500,000 knobs |
| --- | ---: | ---: |
| JSON snapshot | 11.12 MB | 56.28 MB |
| Native local image | 9.20 MB | 46.38 MB |
| JSON encoding | 195.872 ms | 992.572 ms |
| JSON parsing | 296.130 ms | 1,708.244 ms |
| Apply a one-knob patch | 148.624 ms | 780.557 ms |
| Encode local image | 19.778 ms | 108.445 ms |

These are recorded synthetic macOS results, not measurements reproduced during this review. The [benchmark source](../benchmarks/detent_scale_bench.cc) uses uniform namespace sizes and modest override counts; its parity-based construction also makes individual namespaces homogeneous by knob type. It does not exercise skew, large values, large override sets, concurrent ingestion, persistence, or fleet recovery.

The source confirms the expensive operations. [Patch application](../src/detent/json_patch.cc#L118) copies the complete map. [Distributor application](../src/detentd/detentd.cc#L178) serializes, persists, diffs, and compresses complete state under an exclusive lock. [Agent publication](../src/detent_agent/shm_writer.cc#L153) rebuilds the complete image.

Analytical consequences, before compression and protocol overhead:

| Assumption | Consequence |
| --- | --- |
| 500,000 knobs × 128 bytes | 64 MB logical state |
| 50,000 simultaneous full bootstraps | 3.2 TB delivered |
| 1,000 transactions/s × two 128-byte changes | 256 KB/s logical mutation stream |
| Every agent receives that entire stream | 12.8 GB/s aggregate delivery, about 1.11 PB/day |
| 100 equally loaded relays | 128 MB/s each, before headroom |
| Existing 200 ms poll cadence across 50,000 agents | 250,000 polls/s |
| 24 hours of receipts at 1,000 transactions/s | 86.4 million receipts |

At an illustrative 64 bytes per receipt, a one-day idempotency cache already contains 5.53 GB of logical receipt data before indexes, allocators, and replication. Retention is therefore an architectural resource decision.

The baseline's 108.445 ms full-image encoding at 500,000 knobs rules out one complete rebuild per incoming transaction at the assumed rate. Batching helps, but incremental local publication is a requirement for the target design.

The approved scaling mechanisms are:

1. **A small authority group.** Begin with three voters in separate failure domains within one region; five is an availability choice with additional replication cost. Publishers, relays, and agents never vote. A single group is sufficient until a representative benchmark proves otherwise.
2. **Incremental authority application.** Apply bounded transactions to an indexed state structure, emit their changes directly, and avoid complete map copies, diffs, or JSON serialization on the commit path. Snapshot generation consumes a pinned, consistent state version.
3. **Partitionable namespaces.** Use namespace ownership as the future partition boundary. Keep one group initially, but put group identity into revisions and routing now. Partitioned operation cannot promise atomic transactions across groups without additional coordination.
4. **Shared relay history.** Relays retain common immutable change batches and checkpoint objects. Each connection maintains bounded cursor and output state; it must not acquire an unlimited private copy of history.
5. **Subscription reduction.** Admit bounded, canonical namespace subscriptions. Prefer service or namespace bundles over arbitrary per-agent filters, which can create 50,000 independently compiled snapshots and compression caches.
6. **Independent recovery capacity.** Limit snapshot downloads, snapshot compilation, and catch-up traffic separately from ordinary updates. A restart storm must not consume authority resources needed for quorum progress.

The proposed p99 local lookup below 10 µs and regional propagation below 250 ms are benchmark goals. Propagation must be measured from durable write acknowledgement to **local publication**, including relay lag, agent batching, and SDK refresh. CPU-seconds per transaction, agent core-hours, storage write amplification, and delivered GB should accompany latency results. Convert those to dollars or power only when actual hosting prices or hardware measurements exist.

## 2. Edge-Case Analysis

The most urgent findings concern safety.

| Priority | Source finding | Architectural consequence |
| --- | --- | --- |
| P0 | Heartbeat acknowledgement can report an entry that does not exist. | A subsequent command can be considered committed before replication. |
| P0 | Election acceptance uses a majority of received responses followed by more grants than denials. | Five voters can elect a leader with only two affirmative votes. |
| P0 | Leader append truncates an older-term suffix based on local commit knowledge. | Previously committed entries can be lost during leadership change. |
| P0 | Journal recovery mistakes some corruption for an incomplete tail. | Acknowledged history can be silently truncated. |
| P0 | Existing snapshot persistence neither checks stream completion nor establishes file/directory durability barriers. | Atomic pathname replacement does not establish the advertised durable acknowledgement contract. |
| P1 | Mutable mapped payloads and unbounded reader retries remain. | Readers can race writes or wait indefinitely after a writer crash. |

The Raft findings are directly supported by branch arithmetic. They are not observations from executed fault tests.

1. **Phantom replication progress.** [Heartbeat generation](../../Kronuz/cluster/raft.h#L635) sends the actual last log index `n` as `prev_log_index`. A follower computes `entry_index = n + 1` and [reports that as `match_index`](../../Kronuz/cluster/raft.h#L522), including for an empty heartbeat. The leader records it; [the next append immediately invokes commitment](../../Kronuz/cluster/raft.h#L655). Minimal regression: establish a three-node group, deliver heartbeat responses for log end `n`, isolate the leader, then propose entry `n+1`. No node may apply it and no publisher may receive success until an actual majority stores it.
2. **Insufficient affirmative votes.** [Election code](../../Kronuz/cluster/raft.h#L395) tests quorum over grants plus denials, then `grants > denials`. Detent defines quorum as [strict majority of configured members](../src/detentd/raft_delegate.hh#L77). In five nodes, two grants plus one denial satisfy both conditions. A deterministic schedule can give candidate A votes from A/B and a denial from C, while C subsequently receives C/D/E votes in the same term. The test must assert that A remains a candidate and that two leaders cannot arise in that term.
3. **Loss of an inherited committed entry.** [Leader append](../../Kronuz/cluster/raft.h#L647) removes an older-term suffix beyond its locally known commit index. Test a three-node history where A replicates entry `x` to B, commits and acknowledges it, then crashes before B learns the commit index. B wins the next election and receives a new command. `x` must survive; B's incomplete commit knowledge does not authorize deleting it.
4. **Votes cleared within a term.** [Same-term candidate step-down](../../Kronuz/cluster/raft.h#L467) clears `voted_for`; other role transitions also reset it. Test that a node which voted in term `t`, then changes role, cannot grant a different candidate a vote in `t`.

These obligations follow the election, log-matching, and commitment rules in the [Raft paper](https://raft.github.io/raft.pdf). Correcting one observed branch is insufficient: the full transition system needs deterministic schedule testing.

The application contract should be explicit:

1. **Writes are linearizable within one authority group.** Success means the command is durably replicated to a majority and its deterministic state-machine result is available. A timeout means **outcome unknown**. Losing quorum stops successful writes; it does not erase locally readable configuration.
2. **Local reads are snapshot-consistent and eventually current.** A scoped read view pins one published generation. Multiple reads needing a consistent transaction view use that scope. Ordinary reads never wait for the network or consensus.
3. **Authoritative reads need a quorum barrier.** A leader label or recent heartbeat is insufficient after a partition. Provide an explicit strong read mode using current-term quorum confirmation and application through the resulting index. Do not introduce clock-dependent read leases in the first version.
4. **Failures are crash/recovery and network failures.** Handle delay, reordering, duplicates, partitions, process death, failed I/O, and bounded corruption detection. Assume storage honors successful durability barriers. Byzantine voters and a storage stack that lies about durability are outside the guarantee.

The [Raft dissertation](https://web.stanford.edu/~ouster/cgi-bin/papers/OngaroPhD.pdf) supplies the relevant client-session, recovery, snapshot, and read-consistency background.

Transaction semantics need these decisions before implementation:

1. **Bounded atomic batches.** Upsert/delete commands carry an authenticated publisher identity, session/request identity, and optional expected key revisions. Reject duplicate keys within a transaction initially; that removes ambiguous operation ordering.
2. **Server-owned revision allocation.** Keep state revision separate from Raft index. One successful mutation transaction receives one monotonically increasing state revision; all changed keys share it. Rejected operations, control entries, and election no-ops may advance the log without changing state revision.
3. **Deterministic conflicts.** Validate syntax and coarse limits before proposal, then evaluate expected revisions against preceding committed state. Persist or deterministically reconstruct conflict results. Preserve deletion generations sufficiently to prevent delete/recreate ABA errors.
4. **Durable idempotency with an expiry contract.** A UUID receipt cache with a TTL is insufficient: once an old UUID disappears, a delayed retry looks new. Use authenticated, fenced producer sessions with sequence numbers and a bounded result window. Requests from retired/expired sessions must be rejected, not silently reopened. A known sequence whose result was evicted returns an explicit result-unavailable outcome and never executes again.
5. **Canonical request digests.** Digest semantic commands, including preconditions and relevant options. A reused identity with different contents conflicts. Do not assume an arbitrary JSON or binary serialization is canonical.
6. **Explicit replacement semantics.** Preserve legacy JSON ingestion only behind a defined compatibility boundary. A complete replacement from an independent publisher must require an expected namespace revision or an administrative import path. Client-assigned global versions cannot remain the concurrency mechanism.

Distribution must distinguish state progress from transport progress:

1. A cursor identifies cluster/restore epoch, authority group, subscription identity, and revision. Schema compatibility also participates in resume negotiation.
2. Snapshot transfer pins an exact revision `R`; replay begins strictly after `R`. Keep required history available during the transfer or terminate it with a fresh reset.
3. Namespace-filtered streams emit progress watermarks so subscribers can advance across irrelevant transactions.
4. Every frame states its exact base and target. A mismatched base causes resynchronization.
5. Coalescing may skip intermediate states only because this is latest-state configuration. It must preserve transaction atomicity in the state that each subscriber observes. An audit-event API would require a different delivery contract.
6. Keep received, validated, locally published, and durably checkpointed cursors distinct. Resume from the cursor appropriate to the actual recoverable state.
7. A slow subscriber exhausts a fixed byte budget and reconnects through resume/reset. It must never pin unlimited authority history.
8. Restoring an old backup creates an explicit new restore epoch. Revision reset or rollback cannot masquerade as ordinary forward progress.

## 3. Dependency & State Audit

The recommended evolution is an **additive strict consensus module within the existing Kronuz stack**, with a separate legacy-correction track. This respects the preference to improve our Raft implementation without introducing etcd.

Reuse verified mechanisms and interfaces; extract a deterministic protocol core whose transitions explicitly produce storage, network, and application actions. Copying the existing algorithm into a new directory and adding disk calls would retain the defects above.

| Component | Responsibility | Dependency boundary |
| --- | --- | --- |
| Consensus core | Terms, votes, logs, quorum progress, snapshot protocol | No Detent types, filesystem calls, or HTTP |
| Consensus runtime | Timers, authenticated peer channels, ordered completions | Reactor/Asio and injected storage |
| Journal/checkpoint module | Durable record batches, recovery, manifests, reclamation | Opaque bytes and injected I/O |
| Detent state machine | Validation, revisions, CAS, sessions, knob semantics | Consensus application interface |
| Change service | Retention, cursors, reset and subscriptions | Committed changes and authorization policy |
| Immutable publication | Mapping ownership, generation publication, reclamation | Opaque images/pages |
| Local Detent SDK | Lookup, rollout evaluation, defaults and overrides | Immutable views; no required networking stack |

Start these as independently testable modules and targets. Extract further when the interfaces have demonstrated real reuse. Each extracted component needs the repository's documentation, examples, benchmark, and test standard.

The durable Raft boundary requires:

1. Stable cluster and peer identities; configured voting membership independent of gossip liveness.
2. Ordered storage completions. Term/vote changes and appended/replaced log suffixes become durable before protocol responses that depend on them.
3. Correct current-term commitment and actual affirmative majorities; independent follower progress and bounded replication windows.
4. An election no-op and an explicit read barrier mechanism.
5. One owning protocol executor, with disk I/O and snapshot encoding off that executor.
6. Group commit, with completion tokens identifying exactly which messages may now be sent.
7. Storage failure fencing. A member with an uncertain durability result cannot continue voting or acknowledging replication.
8. Fixed membership initially. Replacing a voter requires a separately designed membership protocol or an explicit offline rebootstrap procedure. Editing peer lists independently is not membership management.

A committed application command may be replayed to reconstruct its result; a second full-state fsync per application is unnecessary. Conversely, allocation failure or an application exception after commitment cannot be reported as “the command did not happen.” Fence/recover that member and preserve the publisher's uncertain outcome.

The current prototype is **NO-GO for adoption**:

1. [Journal recovery](../../Kronuz/cluster/consensus/journal.h#L48) trusts an unchecksummed record length. Increasing a valid record's length beyond remaining bytes, while below `max_record`, causes truncation instead of corruption failure. An actually truncated acknowledged record is similarly indistinguishable from an incomplete unacknowledged append.
2. `recover()` returns every record in a vector, making recovery memory proportional to all retained history.
3. `append()` synchronizes every record; there is no batch frontier or group-commit contract.
4. Replacement rewrites the entire record set and relies on locking the replaced inode. Durable namespace ownership, stale temporary files, and post-rename failure behavior need explicit design and fault tests.
5. Recovery errors do not consistently enter the same fenced state as append errors.
6. The codec's per-frame bound does not provide connection, decoded-object-count, nesting, or total recovery bounds.

The successor journal needs checksummed framing metadata, a sequence/identity model, verified durable frontiers, incremental replay, segmented reclamation, and atomic suffix-replacement semantics. A recovery rule may discard an incomplete **unacknowledged** tail only when the durable frontier proves it is outside acknowledged history.

Checkpoint publication must write and synchronize immutable content, publish and synchronize a manifest, and only then reclaim covered segments. Checkpoints include configuration identity, last included index/term, state revisions, schema version, session/idempotency state, and integrity metadata.

Existing `Kronuz/storage` is useful evidence and potential reuse, but is **not an approved Raft WAL**:

1. Its [v2 format rationale](../../Kronuz/storage/ARCHITECTURE.md) assumes atomic 512-byte sector writes for a single mutable header. That is not a portable storage guarantee. SQLite's [hardware assumptions](https://www.sqlite.org/atomiccommit.html) explicitly distinguish advertised atomic-write capabilities from the default case.
2. Its [async synchronization hook](../../Kronuz/storage/storage.h#L1092) has no completion result. The subsequent data-sync → header-write → header-sync sequence therefore does not establish those completed barriers under an asynchronous hook.
3. Xapiand's WAL [selects asynchronous synchronization](../../Kronuz/Xapiand/src/database/wal.cc#L68). Its recovery/availability choices cannot silently become Detent's acknowledged-write guarantees.
4. The default record framing does not itself supply all the integrity properties Detent requires.

Reuse suitable I/O and codec mechanisms, and evaluate immutable checkpoint blobs separately. Do not redesign Xapiand's storage profile merely to land Detent.

`Kronuz/flume` is also a candidate requiring review. Its [receiver](../../Kronuz/flume/flume.h#L199) can accumulate a peer-declared frame indefinitely and decompress without an output cap. Its synchronous sender also needs genuine flow control if adapted to an asynchronous socket queue.

For Xapiand compatibility:

1. Preserve legacy public types, delegate seams, and wire encoding while developing strict consensus under an explicit target and protocol version.
2. Pin reviewed dependency revisions for reproducible integration. [Xapiand currently fetches `cluster` at `main`](../../Kronuz/Xapiand/CMakeLists.txt#L797), so publishing a library change is otherwise an implicit consumer upgrade.
3. Add deterministic reproductions for the discovered legacy defects, then correct them in individually reviewable changes. Preserving compatibility does **not** mean preserving known unsafe decisions indefinitely.
4. Distinguish wire compatibility from safety during mixed-version operation. Old peers can still report invalid replication progress or elect incorrectly; a byte-compatible rolling upgrade does not automatically acquire the corrected guarantee.
5. Gate fixes on legacy election, relinquish, discovery, bootstrap, two/three/five-node, partition, and Xapiand cluster/E2E tests. Include old/new combinations in both leader and follower roles.
6. Do not place old and strict durable peers in one voting group. Xapiand's eventual strict-mode migration is separate from Detent's adoption; no wholesale Xapiand rewrite is prerequisite.

## 4. Security & Scale Review

TLS, authorization, and resource limits must cover the first production transport. They cannot be added only after the scale harness succeeds.

1. **Identity.** Use authenticated peer identities tied to configured cluster/node IDs. Authenticate publishers and agents independently. Possessing the current multicast token is not sufficient authority.
2. **Authorization.** Enforce publisher write scope and subscriber read scope at namespace boundaries. Canonicalize namespace names before authorization, caching, and matching. Cache keys include authorization/subscription identity to prevent cross-scope leakage.
3. **Bounds.** Cap wire bytes, decompressed bytes, fields, nesting, key lengths, operations, override counts, aggregate state, inflight proposals, per-peer replication bytes, partial snapshots, subscriber queues, and snapshot workers. Admission counts bytes as well as requests and applies per-principal fairness.
4. **Transport isolation.** Keep control/heartbeat traffic from being blocked behind bulk snapshots. Persistent point-to-point replication and agent streams need asynchronous completion; one waiting worker per connection does not meet the fleet objective.
5. **Compatibility.** Negotiate frame/schema capabilities before accepting commands. Unknown required semantics must be rejected. An older voter must never silently ignore a field affecting replicated state.
6. **Local trust.** Keep service-owned directories, safe file opening, and explicit read permissions. A world-readable system-agent image is a host-wide disclosure boundary. Revoking a subscription cannot make data already delivered to an offline agent disappear.
7. **Operational fairness.** Limit handshake and reconnect rates, use randomized backoff, and bound high-cardinality metrics. Observe quorum health, durable/apply lag, retained bytes, snapshot backlog, stream lag, local publication failures, and fsync latency.

For binary encoding, use a versioned schema and explicit framing; keep native mapped structs out of the network protocol. The leading candidate is **Protobuf without gRPC**, subject to a measured comparison with a narrowly specified MessagePack schema and JSON+zstd. [Protobuf's evolution rules](https://protobuf.dev/programming-guides/proto3/) are an available mechanism; they do not eliminate application bounds or define canonical request digests.

The chosen codec must win on the actual balance of compressed bytes, encode/decode CPU, memory, generated/runtime footprint, and cross-language support. CRC detects corruption; it does not authenticate a sender.

Host-local publication needs a concrete lifetime design:

1. Replace the mutable 4 MiB mapping with immutable, dynamically sized generations published only after validation and construction succeed.
2. Retain the previous published root on any failure. Agent acceptance must not advance the resume cursor merely because an upstream payload was parsed.
3. For target-rate updates, use immutable indexed pages or segments with path copying; rewrite affected data and index paths rather than the full state. Batch within the propagation budget.
4. Pin all mappings needed by a read view before exposing it to application reads. If opening an old generation races reclamation, retry before publishing that view.
5. Never truncate or overwrite a mapped generation. Unlink/replacement and mapping lifetime must follow supported OS semantics.
6. Bound checkpoint construction and abandoned generations. Applications holding views indefinitely can retain physical storage; the SDK lifetime contract and operational accounting must acknowledge that.
7. Preserve live/default/override layering. Deleting a live knob must continue to expose its baked default where that is the existing semantic.
8. Store optional restart checkpoints in the persistent state directory. Runtime mappings alone are not reboot recovery.

The existing [shared-memory layout](../src/detent_agent/shm_format.hh#L51) documents plain payload races. The [reader](../src/detent_agent/shm_reader.cc#L176) retries indefinitely when publication remains odd. Neither issue should survive the replacement design; WG21's [bytewise atomic-copy proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0603r0.htm) explains why ordinary seqlock retry does not legalize concurrent plain C++ payload access.

Keep daemon operation aligned: foreground default, optional `--daemonize`, and read-only `--status`. Status should report process identity plus role-appropriate useful work. For the authority, expose quorum/storage readiness and durable/applied revisions; for the agent, expose published revision, source connectivity, lag, and last publication failure. Apply a deadline to status inspection, and never auto-start from it.

## Implementation stages and acceptance gates

Implementation remains paused for this review. The following is the concrete sequence when work resumes:

| Stage | Deliverable | Required gate |
| --- | --- | --- |
| 0 | Contracts, fault model, workload matrix, dependency inventory | Architect review resolves P0 design ambiguities |
| 1 | Deterministic Raft counterexamples and isolated legacy corrections | Each failure reproduced before correction; Xapiand compatibility matrix |
| 2 | Durable journal/checkpoint interfaces and deterministic strict core | Crash-prefix, torn-write, failed-sync, partition, duplicate/reordered-message, and recovery invariants |
| 3 | Bounded transaction API, deterministic state application, sessions/CAS | Concurrent histories, retry-after-timeout, delete/recreate, and restart result consistency |
| 4 | Portable snapshot/delta schema and immutable dynamic local views | Cross-version decoding, malicious lengths, process-kill publication, native memory/concurrency checks |
| 5 | Resumable streams, subscriptions, relay fan-out, incremental local publication | Exact snapshot handoff, slow clients, retention resets, bounded memory/disk |
| 6 | Representative system qualification | Mixed writers/readers, failover, overload, cold starts, and multi-host fleet tests |

Stage 1 must not become an unlimited detour into a complete Xapiand consensus migration. Fix and validate the discovered defects, document the remaining volatile-mode limits, and maintain isolation for strict-mode development.

Before durable-mode adoption, require:

1. Model-level invariants for one leader per term, one vote per term, log matching, leader completeness, and state-machine agreement.
2. A controllable I/O simulator that explores crashes around writes, syncs, renames, directory syncs, manifest installation, and reclamation. Its device assumptions must be written down.
3. Real-process kill/restart tests that trace every successful publisher acknowledgement through recovery. `SIGKILL` tests alone do not simulate power loss.
4. Fault cases covering disk-full, short writes, read-only storage, failed sync, stale snapshots, lost acknowledgements, leader changes, quorum loss, and restoration.
5. Linearizability checks for authoritative operations and atomic-generation checks for local readers.
6. Platform-specific builds and relevant ASan/UBSan/TSan checks using the workspace's supported toolchains.
7. A compatibility matrix spanning stored formats, wire versions, SDK/agent versions, and Xapiand consumers.

For scale, sweep state size, update rate, mutation size, subscription overlap, publisher contention, and recovery concurrency independently. Run enough hosts that the load generator, socket limits, and loopback behavior are not the result. Capture p50/p95/p99, queue age, resident memory, CPU, network, fsync latency, and disk amplification. No unbounded queue growth at the sustained design point is a required outcome.

## Migration, rollback, and remaining decisions

Use a one-way, explicit authority cutover. Validate an exported legacy snapshot, bootstrap a new durable cluster under a new identity, compare state hashes in shadow operation, then switch the write endpoint and agent source configuration. Avoid dual independent writers.

Rollback before durable writes can return to the unchanged legacy deployment. Once the new authority acknowledges writes, rollback requires a controlled export/cutover or a compatible older durable binary. Restarting the old JSON-backed authority against stale state is not a rollback procedure.

The following product choices remain open, with recommendations:

1. **Failure domain and regional write availability:** Begin with one regional three-voter authority and regional relays. Revisit cross-region voting only if surviving total-region loss with continued writes is a requirement.
2. **Staleness policy:** Continue serving last-known-good values by default, with explicit age/connectivity reporting and an opt-in caller policy for unacceptable staleness.
3. **History and retry horizon:** Choose a bounded producer-session/result window and an explicit audit retention policy. Do not promise unlimited receipt or audit retention.
4. **Initial atomicity scope:** Keep transactions within one namespace to preserve a straightforward future partition boundary unless a real cross-namespace use case requires broader scope.
5. **Real workload:** Obtain representative key lengths, override distributions, update bursts, namespace overlap, hardware, and deployment constraints before declaring an SLO.

These are design assumptions and later release decisions, not reasons to request permission for ordinary reversible implementation work.

## Required review cadence

Repeat this high-effort frontier Architect pass at every consequential architecture fork and after each implementation milestone. Each pass must cover **Scalability Check, Edge-Case Analysis, Dependency & State Audit, and Security & Scale Review**.

Record the exact reviewed revisions, evidence, unresolved findings, and an explicit **GO / CONDITIONAL GO / NO-GO** verdict. Consensus/storage changes need invariant and fault-test review; format/SDK changes need compatibility and lifetime review; fleet-facing changes need measured overload and recovery review.

The current verdict is **CONDITIONAL GO on the architectural direction; NO-GO on prototype adoption and production durability claims**. The next implementation work, once resumed, begins with the contracts and deterministic counterexamples above.
