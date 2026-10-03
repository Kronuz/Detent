# Durable authority and fleet distribution implementation

**Status:** Implementation authorized; legacy safety prerequisites validated locally; durable storage in progress.

The [Validated Architectural Blueprint](VALIDATED_ARCHITECTURE.md) governs this work. Each stage receives a high-effort frontier Architect review. Source findings, reproduced failures, and qualification results are recorded separately; completion of the legacy corrections does not establish durable consensus or fleet capacity.

## Stage 1: legacy Raft safety

The corrected library is developed in the sibling `Kronuz/cluster` repository. Detent's local integration uses `cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CLUSTER=/Users/kronuz/code/Kronuz/cluster`. Library changes remain local until explicitly published; the existing remote dependency cannot supply unpublished corrections.

| Change | Evidence | State |
| --- | --- | --- |
| Verified heartbeat progress, data acknowledgement retries, old-follower response handling | Four initial heartbeat checks failed on the preceding library source. Thirteen expanded checks pass after correction, including old response shapes and same-identity follower restart. All three cluster CTests and all 23 Detent CTests pass. | Architect GO; committed in cluster `23407e8` |
| Affirmative voting majority | Five configured voters remain unelected with two grants and a denial, including duplicated responses; a third distinct grant elects the candidate. These checks failed before correction and pass afterward. | Architect reviewed; committed in cluster `7c7dd86` |
| One vote per term across role changes | Delayed self-loopback, same-term leader traffic, and explicit step-down failed before correction. Those checks, elected-leader step-down, and a new-term vote pass after correction; all three cluster CTests pass. | Architect reviewed; committed in cluster `6f6fcc3` |
| Retention of inherited log entries | A successor with an unknown committed prefix loses it on append before correction. The corrected schedule applies that inherited entry before the new-term command. All three cluster CTests and all 23 rebuilt Detent CTests pass. | Architect GO; cluster `1697e56` |
| Higher-term handling and stale traffic | Nine initial checks failed before correction. Full decoding precedes term observation and role filtering; stale traffic receives current-term responses. Higher-term traffic, malformed fields, and index overflow schedules pass. | Architect GO; cluster `5b2a141` |
| Length-decoder overflow | Three integer-overflow schedules failed before correction. Encoded overflow and narrowing are rejected without modifying output; maximum valid values round-trip. An overflowing encoded term cannot depose a leader. | Architect GO; cluster `5f583e5` |
| Term exhaustion | Four schedules failed before correction. The maximum term and ballot survive start, step-down, and immediate leadership hand-off; the preceding term can still elect the final leader. | Architect GO; cluster `510e120` |

The initial heartbeat patch incorrectly retained a follower's replication evidence after a same-identity restart with an empty volatile log. A deterministic regression reproduced the introduced failure. A rejection now invalidates that peer's evidence and permits replay without lowering the leader's committed index; both the regression and the previously failing Detent restart scenario pass.

Final-header qualification passes all three cluster CTests in native and AddressSanitizer/UndefinedBehaviorSanitizer Release builds, plus all 23 rebuilt Detent CTests. The final focused safety test also passes after adding the Architect's immediate hand-off coverage and replacing fixed-duration replay waits with predicate-driven event pumps.

Xapiand qualification uses sibling source overrides and a fresh `.scratch/build`. Its default build encountered a Lua 5.5/sol2 compatibility error. The supported `-DLUA=OFF` profile then exposed an existing declaration/definition mismatch in its scripting-unavailable stub. The isolated declaration fix is committed as Xapiand `877f46ab2`, and that profile builds the complete binary. Both two-node and three-node discovery, replication/remote, and distributed-search harnesses pass with the final corrected headers. Lua-disabled qualification does not establish default-build compatibility; these same-version local harnesses do not qualify mixed-version voting or multi-host behavior.

## Remaining stages

1. Durable journal/checkpoint interfaces and deterministic strict consensus core.
2. Bounded transactions, incremental state application, CAS, and producer sessions.
3. Portable snapshot/delta schema and immutable dynamic local views.
4. Resumable streams, namespace subscriptions, regional relays, and incremental publication.
5. Representative overload, recovery, multi-host, and platform qualification.

Each stage requires the blueprint's correctness, compatibility, resource-bound, and measurement gates. Production guarantees remain unqualified until their corresponding gates pass.

## Stage 2: durable storage and strict consensus

The additive sibling `cluster/journal/` module, committed as cluster `55e3072`, provides a separately buildable, application-independent opaque batch journal. A checksummed manifest identifies the durable frontier; complete framing and payload checks precede incremental replay. The POSIX backend confines filenames to an owned directory, holds a stable owner lock, and fences uncertain failures. Recovery completes a namespace barrier before exposing its selected frontier, including after process death during manifest publication. The rejected untracked prototype is retained only in sibling ignored scratch for comparison and is no longer an adoption candidate.

The Architect reviewed scalability, edge cases, dependency/state boundaries, and security/scale, with GO for this append/recovery milestone after correcting 32-bit offset arithmetic and extending failure injection through recovery tail truncation. Native and ASan/UBSan tests pass 1,616 check invocations. All four root cluster CTests pass, and the standalone module builds without fetching reactor or Asio. The tests model before/after I/O effects and both namespace outcomes; actual filesystem/hardware power-failure qualification remains pending.

The initial local measurement uses 64 groups of opaque 256-byte operations. Median durable batch latency is approximately 63 ms on this Intel Mac; groups containing 1, 32, and 256 operations deliver approximately 16, 509, and 4,074 opaque operations/second. This measures storage barriers rather than transaction or authority capacity. Full results, CPU seconds, executable sizes, reproduction commands, and limitations are in [the journal documentation](../../Kronuz/cluster/journal/README.md).

The additive deterministic strict core and semantic journal adapter are committed as cluster `7b1e9ba`. The Architect's four-dimensional review gives GO for the transport-free, fixed-voter milestone after correcting read-flight preemption and fixed-time probe feedback. Native and ASan/UBSan suites pass 4,808 check invocations; all five root cluster CTests pass. The real journal integration test reconstructs committed consensus state after reopening, and the standalone demo retains its first command while appending a second across process restarts. Three- and five-voter seeded schedules cover 1,000 steps each with drops, duplicates, reordering, partitions, and durable restarts. These schedules do not certify production consensus or fleet throughput.

The strict core persists term/vote/log/commit transitions before dependent effects, retains reliable application completions while busy, and authorizes reads through fresh quorum probes. Fixed-voter membership and payload/entry budgets remain separate from discovery and wire decoding. [The core's host contract](../../Kronuz/cluster/consensus/README.md) records authentication, timing, application, and recovery requirements.

Checkpoint publication, journal reclamation, snapshot installation, and authority transport integration remain in progress. The next reviewed milestone is local immutable-generation publication, followed separately by portable snapshot transfer. A network snapshot must never import a sender's ballot, hard state, or local voter identity. The journal and strict core do not add persistence to the legacy `cluster::Raft` API.
