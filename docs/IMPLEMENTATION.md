# Durable authority and fleet distribution implementation

**Status:** Implementation authorized; safety prerequisites in progress.

The [Validated Architectural Blueprint](VALIDATED_ARCHITECTURE.md) governs this work. Each stage receives a high-effort frontier Architect review. Source findings, reproduced failures, and qualification results are recorded separately; completion of the legacy corrections does not establish durable consensus or fleet capacity.

## Stage 1: legacy Raft safety

The corrected library is developed in the sibling `Kronuz/cluster` repository. Detent's local integration uses `cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CLUSTER=/Users/kronuz/code/Kronuz/cluster`. Library changes remain local until explicitly published; the existing remote dependency cannot supply unpublished corrections.

| Change | Evidence | State |
| --- | --- | --- |
| Verified heartbeat progress, data acknowledgement retries, old-follower response handling | Four initial heartbeat checks failed on the preceding library source. Thirteen expanded checks pass after correction, including old response shapes and same-identity follower restart. All three cluster CTests and all 23 Detent CTests pass. | Architect GO; committed in cluster `23407e8` |
| Affirmative voting majority | Five configured voters remain unelected with two grants and a denial, including duplicated responses; a third distinct grant elects the candidate. These checks failed before correction and pass afterward. | Architect reviewed; committed in cluster `7c7dd86` |
| One vote per term across role changes | Delayed self-loopback, same-term leader traffic, and explicit step-down failed before correction. Those checks, elected-leader step-down, and a new-term vote pass after correction; all three cluster CTests pass. | Architect reviewed; committed in cluster `6f6fcc3` |
| Retention of inherited log entries | A successor with an unknown committed prefix loses it on append before correction. The corrected schedule applies that inherited entry before the new-term command. All three cluster CTests and all 23 rebuilt Detent CTests pass. | Architect review in progress |
| Higher-term handling and stale traffic | Explicit message schedules planned | Pending |

The initial heartbeat patch incorrectly retained a follower's replication evidence after a same-identity restart with an empty volatile log. A deterministic regression reproduced the introduced failure. A rejection now invalidates that peer's evidence and permits replay without lowering the leader's committed index; both the regression and the previously failing Detent restart scenario pass.

Xapiand qualification uses sibling source overrides and a fresh `.scratch/build`. Its default build encountered a Lua 5.5/sol2 compatibility error. The supported `-DLUA=OFF` profile then exposed an existing declaration/definition mismatch in its scripting-unavailable stub. The isolated declaration fix is committed as Xapiand `877f46ab2`, and that profile builds the complete binary. The two-node discovery, replication/remote, and distributed-search harness passes with the local heartbeat/majority corrections. Final-header and three-node qualification remain pending; Lua-disabled qualification does not establish default-build compatibility.

## Remaining stages

1. Durable journal/checkpoint interfaces and deterministic strict consensus core.
2. Bounded transactions, incremental state application, CAS, and producer sessions.
3. Portable snapshot/delta schema and immutable dynamic local views.
4. Resumable streams, namespace subscriptions, regional relays, and incremental publication.
5. Representative overload, recovery, multi-host, and platform qualification.

Each stage requires the blueprint's correctness, compatibility, resource-bound, and measurement gates. Production guarantees remain unqualified until their corresponding gates pass.
