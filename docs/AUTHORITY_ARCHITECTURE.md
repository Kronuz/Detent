# Bounded authority application and runtime

**Status:** Complete bounded authority component, with consolidated Architect source GO and native/sanitizer qualification. This component follows cluster `99d01be` and Detent `5778aeb`; additive Worker provenance accessors are cluster `a44c991`. Authenticated multi-node transport and completion-based IO follow the [next validated blueprint](ASYNC_AUTHORITY_ARCHITECTURE.md).

The component contains the authority state machine, owned durable Worker integration and an opt-in loopback single-voter daemon profile. The existing legacy distributor stays available. Authenticated multi-node sockets are the next service component.

## Validated Architectural Blueprint

1. Scalability: Immutable balanced trees replace complete-map copying. Knob definitions are shared; each mutation replaces affected paths. A capture pins roots in constant time. Bound current, capture and incoming state ownership, and reclaim obsolete nodes through budgeted maintenance. Group up to 64 transactions within the Worker command limit, with at most 5 ms grouping delay.
2. Edge cases: Transactions are atomic within one namespace. Exact present revisions protect existing keys; absence checks require the namespace revision and therefore detect delete/recreate ABA without unbounded key tombstones. Changed transactions allocate server revisions; identical upserts and absent deletes return NoChange. Compare preconditions against the pre-transaction root. Capacity rejection and conflicts retain deterministic results.
3. Dependency and state: Keep the application library independent of consensus, IO and HTTP. Runtime owns Worker, application delivery, recovery, capture and matching semantic activation. Success follows committed application, never ProposalPlaced. Strong reads and result lookups use a quorum barrier. Persist identities, semantic limits, applied index/term, namespace revisions and complete producer retry state in versioned images.
4. Security and scale: Bind producer identity and namespace authorization through trusted adapter credentials. Initial service binds loopback and rejects legacy multicast configuration. Bound bodies before buffering and separately bound requests, aggregate bytes and waiters. No fleet-capacity claim follows from local tests.

## Sessions and limits

Producers register explicitly. Open compares the previous generation and binds a retry nonce; Close fences the current generation. Keep the bounded producer registry after closure. Transactions use contiguous sequence numbers, with a high-water mark and the last 256 fixed-size results in the current generation. Retained same-content retries return their original outcome; changed contents conflict. Evicted results return ResultUnavailable and never execute again. Older and closed generations stay fenced. There is no wall-clock expiry.

Initial application limits are 500,000 live knobs, 4,096 namespace records, 1,024 producers, 128 operations per transaction, 256 KiB canonical requests, 16 KiB definitions and 256 overrides. Live logical bytes and maximum image bytes have separate limits. Empty namespaces retain their last revision and consume a namespace slot. Credentials remain outside images. Startup must fund the maximum application image and receiver-local bundle in the Store replacement pool.

The internal command codec uses a versioned canonical semantic representation, including authority identities and preconditions. Replicated batch framing (`DTABAT02`) binds all ten semantic limits before any command is applied, so differently configured replicas or pre-checkpoint recovery fail closed rather than changing acknowledged outcomes. Mutation order and override insertion order do not change SHA-256 identity. This internal codec does not select the public Protobuf/MessagePack/JSON transport comparison.

## Acceptance gates

1. Deterministic histories cover CAS conflicts, atomic failure, no-change, capacity, receipt eviction, changed-content retries, reordered opens, retired generations, absence ABA and exhaustion.
2. Three real Workers on independent POSIX stores agree on values and receipts across partitions, leader changes, retries, recovery and snapshot catch-up.
3. Checkpoint and recovery retain sessions and receipts. Process-kill tests trace acknowledged results across restart; they do not simulate power loss.
4. The opt-in daemon exercises transaction/read/status, overload, restart and persisted result lookup. Existing Detent and cluster regressions, native tests and relevant sanitizers remain green.
5. Compare complete JSON patching with incremental application at 100,000 and 500,000 knobs. Include overrides, hot namespaces and grouped publishers; record allocations, CPU, RSS, checkpoint pauses and storage amplification.

The fresh pre-implementation synthetic baseline takes 152.337 ms and 798.229 ms for a one-knob JSON patch at 100,000 and 500,000 knobs, respectively, with three repetitions. Run `build/benchmarks/detent_scale_bench COUNT 3` to reproduce. Paired application and durable measurements are recorded below; the original baseline used RapidJSON 1.1.0, while paired tests use the updated pinned revision.

## Runtime and ownership

`State` publishes immutable AVL roots only after a complete transaction or grouped Raft entry. Captures retain the current roots; definitions and producer records are shared. Updates collect obsolete paths according to allocations made during the operation. Each service turn additionally collects up to 128 nodes from each retired map. The runtime owns one incoming candidate and one retired root set; admission of incoming decoding pauses until retirement completes. Invalid candidates enter the same scheduled reclamation path. Shutdown may release the remaining store synchronously.

Image encoding emits at most 64 KiB per call. Image decoding retains one incomplete row plus a 64 KiB input chunk, processes at most 128 rows or namespace-correlation checks per service turn, and constructs an unpublished root. Recovery uses the same decoder. Header identities, semantic limits, ordering, receipt windows and the exact applied index/term must validate before activation. A producer row is bounded by its configured receipt count; the default maximum is approximately 20 KiB. Physical allocator overhead is outside the logical byte limit, and benchmark RSS includes that overhead.

Automatic checkpoints begin after retained entries or retained command payload bytes reach half their configured limits. The complete captured application image and receiver-local consensus bundle must fit the replacement pool. Checkpoint preparation remains an admission boundary; its measured executor pauses must be assessed against the eventual production service budget.

## Loopback daemon profile

Build with `-DDETENT_BUILD_AUTHORITY=ON` and reviewed sibling sources supplied through `-DFETCHCONTENT_SOURCE_DIR_CLUSTER=/path/to/cluster` and `-DFETCHCONTENT_SOURCE_DIR_SHA256=/path/to/sha256`. The profile defaults off while its shared library changes are local. The application-only library and state tests build independently of the distributor.

Create a configuration owned by the service user or root, with mode `0600` (or controlled group-readable `0640`). The state directory is resolved relative to this configuration; its parent must already exist. Use independent, nonzero 128-bit identities and a randomly generated bearer token. These illustrative values belong only in tests:

```json
{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "directory": "authority-data",
  "credentials": [{
    "token": "replace-with-a-random-token-at-least-32-characters",
    "producer": "publisher",
    "namespaces": ["payments"]
  }]
}
```

```sh
detentd --authority authority.json
detentd --authority authority.json --daemonize
detentd --status
```

The authority binds `127.0.0.1` and defaults to port 8317. It rejects legacy multicast and legacy persistence overrides. Service managers should run it in the foreground; use the configuration and directory ownership conventions in [SERVICES.md](SERVICES.md). This profile has one voter and local disk durability, with no replicated availability. It does not serve legacy `/v1/snapshot` distribution to agents; that integration follows the authenticated transport component.

Each request uses `Authorization: Bearer TOKEN` and carries the exact `cluster`, `group` and `epoch` fields. Producer identity comes from the credential, never JSON. Duplicate JSON fields, including nested knob definitions and override objects, are rejected before proposal. Namespace authorization covers transactions and strong reads. Result lookup is limited to that producer's session. Credential rotation preserves the producer name; changing restore identity fences older requests.

| Endpoint | Additional JSON fields |
|---|---|
| `POST /v2/open` | `expected_generation`, nonzero 32-hex-character `nonce` |
| `POST /v2/close` | `generation` |
| `POST /v2/transactions` | `generation`, `sequence`, `namespace`, optional `expected_namespace`, `operations` |
| `POST /v2/read` | `namespace`, unique `keys` (at most 128) |
| `POST /v2/result` | `generation`, `sequence` |
| `GET /v1/health` | None; loopback status is unauthenticated |

An operation supplies `key` and `value`, using the existing bool/int definition schema (`{"type":"int","default":42}`) or `null` for deletion. Omit `expected_revision` for unconditional replacement, use a positive modification revision to compare an existing key, or use zero together with `absent_namespace_revision` to compare absence.

Responses carry authority identity and compact outcomes. `applied` and `no_change` are terminal successes; a retained retry returns the original outcome and revision. Conflict, logical capacity rejection and revision exhaustion also consume the next sequence. `outcome_unknown` after a timeout requires retrying the same generation, sequence and contents. Strong reads and cached result lookup use a quorum barrier. Never advance sequence solely because the connection disappeared.

The adapter reserves the maximum 256 KiB body before buffering, with a 16 MiB aggregate reservation, 1,024 request slots and at most 16 admitted requests per producer. One logical transaction per producer can be outstanding. Up to 64 different producers share a Raft entry within its encoded byte limit and a 5 ms grouping window. The Asio transport transfers response authority into an owned deferred capability with a 5-second deadline. Parsing may use the bounded HTTP worker pool, but waiting for Raft does not occupy a worker. Request and accepted reply leases survive until original socket IO settlement; separate ingress and egress reservations default to 16 MiB. Slow readers retain only their selected response, not a historical State root. Owner-side reads encode into a buffer that checks the actual JSON bound before each byte, using only one definition DOM at a time; oversized selections return `reply_bound`. Authentication and body-admission failures close the connection before buffering because the framework's body hook has no early error-response seam.

## Explicit migration

Import only into a new, separate authority directory, with writers stopped until verification completes:

```sh
detent-authority-admin import authority.json legacy-snapshot.json
```

The tool validates the bounded legacy document, applies namespace-scoped batches within transaction limits, compares every resulting definition with the source, and publishes an application checkpoint. Import requires a single voter. Legacy version numbers never become authority revisions. A failed import leaves a directory for inspection; it cannot be silently resumed or overwrite an existing authority. The import session consumes one producer slot.

Stop the authority before a rollback export. Supply an explicit legacy version greater than the destination's last accepted version:

```sh
detent-authority-admin export authority.json rollback-snapshot.json 1000
```

The storage lock excludes a running daemon. Export replays the selected voter's durable committed cut as a follower without advancing election time, including stores from a three-voter group. That cut can lag the group's latest commit; export does not contact a quorum. It creates a new `0600` output file and refuses to overwrite an existing file. The legacy output uses file and parent `fsync`; the stronger backup durability contract is part of the pending [restore component](RESTORE_ARCHITECTURE.md). Verify and publish through the destination's ordinary interface before switching writers. A lower version will be rejected by the legacy destination.

## Qualification evidence

The native suite passes 30 tests. Seven authority ASan/UBSan tests pass, including an actual HTTP daemon ACK followed by SIGKILL/restart/retry and a bounded migration/export round trip. The three-Worker fixture uses separate POSIX stores and exercises partition, leader replacement, snapshot catch-up, lost success responses and restart. Process-kill publication tests distinguish acknowledged and uncertain results; they do not simulate power loss.

The sanitizer pass found pre-existing dependency defects: RapidJSON 1.1.0 fails compilation under Clang 23, and SHA-256 finalization forms an out-of-bounds intermediate pointer on two-block padding. Separate local commits update the RapidJSON pin and fix the padding arithmetic, with boundary digest vectors unchanged. A missing direct `<exception>` include in the HTTP polling source was also corrected separately. Clang qualification uses an explicit macOS SDK to keep CMake's compression headers and compiler's C++ wrappers on the same SDK; the controlled baseline did not justify a compressors source change.


## Paired application measurements

These October 4, 2026 Release measurements use the same bool/int definitions for both implementations on the Intel Mac. Half the initial knobs are boolean percentage rollouts with the indicated number of group overrides; the others are integers. Every loaded definition is compared before timing. The measured mutation repeatedly replaces one existing knob with an integer. It does not measure replacing a large override definition on every update.

| Knobs | Namespaces | Overrides per bool | Legacy patch median ms | Authority update median ms | Authority CPU ms/update | Allocated tree nodes/update | Application image bytes |
|---|---|---|---|---|---|---|---|
| 100,000 | 100 | 2 | 133.708059 | 0.012666 | 0.014105 | 26 | 8,602,246 |
| 500,000 | 100 | 2 | 668.598955 | 0.011275 | 0.011443 | 27 | 43,362,246 |
| 100,000 | 1 | 2 | 128.899819 | 0.009680 | 0.009919 | 19 | 8,509,582 |
| 10,000 | 1 | 256 | 289.968249 | 0.008847 | 0.008974 | 15 | 32,073,338 |

The legacy path copies the complete snapshot; the authority path validates the command and copies affected AVL paths. These are 21 legacy and 1,000 authority samples, without IO, HTTP or replication. At 1,000 application updates/second, the 500k sample implies approximately 0.0114 CPU cores for this application work alone; it does not establish achievable durable throughput or fleet savings. No power or pricing measurements are available.

Immutable encoder capture takes 7.226–11.693 microseconds in these runs. Full image encoding takes 37.167–190.602 ms in 64 KiB chunks, with maximum observed encoding turns of 0.248–0.511 ms. The paired benchmark's peak RSS includes both complete corpora and repeated legacy copies: 275,320,832 bytes at 100k and 1,100,783,616 bytes at 500k. These figures are not authority daemon RSS.

```sh
build/benchmarks/detent_authority_bench 100000 uniform 2
build/benchmarks/detent_authority_bench 500000 uniform 2
build/benchmarks/detent_authority_bench 100000 hot 2
build/benchmarks/detent_authority_bench 10000 hot 256
```

## Durable single-voter measurements

The durable benchmark uses actual POSIX storage, 64 established producer sessions, one hot namespace and the same two-override bool/int corpus. Initialization is excluded from steady update timing. It measures 100 groups; each active producer changes one existing key per group. There is no HTTP, TLS, peer replication or concurrent client load.

| Knobs | Active producers/group | Transactions measured | Amortized ACK ms/transaction | CPU ms/transaction | Maximum update storage turn ms | Peak RSS bytes |
|---|---|---|---|---|---|---|
| 100,000 | 1 | 100 | 127.776986 | 3.241290 | 78.671 | 55,619,584 |
| 100,000 | 64 | 6,400 | 2.033560 | 0.082847 | 102.583 | 57,548,800 |
| 500,000 | 64 | 6,400 | 2.040765 | 0.085297 | 114.795 | 201,646,080 |

Amortized ACK time divides group wall time by its transaction count. Each group member waits for the complete group, approximately 128–131 ms in these samples; this is not a request latency distribution. Grouping 64 producers yields approximately 490 transactions/second on this local disk. The 500k grouped sample consumes approximately 0.042 process CPU cores at that measured rate, excluding HTTP, TLS and replication. This is CPU demand, not a measured monetary or power saving.

| Knobs | Active producers | Checkpoint capture us | Admission wait ms | Checkpoint wall ms | Maximum checkpoint/maintenance turn ms | Application image bytes | Disk bytes at publication | Disk bytes after maintenance |
|---|---|---|---|---|---|---|---|---|
| 100,000 | 1 | 20.808 | 0.021 | 214.553 | 63.776 | 6,963,426 | 13,515,457 | 6,963,914 |
| 100,000 | 64 | 51.342 | 0.051 | 213.332 | 63.619 | 7,465,690 | 15,118,421 | 7,466,178 |
| 500,000 | 64 | 16.268 | 427.502 | 468.285 | 64.765 | 35,715,690 | 71,728,716 | 35,716,178 |

Checkpoint capture remains separate from admission waiting. At 500k, an already active automatic checkpoint delays explicit admission by 427.502 ms; the subsequent immutable capture takes 16.268 microseconds. Publication temporarily retains old files until bounded maintenance completes. The measured post-maintenance store is application image size plus 488 bytes in these fixtures. Disk footprint and encoded command bytes do not measure cumulative device writes or physical write amplification.

These observed maximum turns include synchronous filesystem calls. The completion-based IO component must keep those waits off the owner/reactor path and separately measure CPU work, timer delay and native/fallback operations. Process kills validate restart behavior; filesystem and hardware power-loss qualification remains separate.

Use a new directory for each run:

```sh
build/benchmarks/detent_authority_durable_bench .scratch/authority-measure-single-100k 100000 1
build/benchmarks/detent_authority_durable_bench .scratch/authority-measure-100k 100000 64
build/benchmarks/detent_authority_durable_bench .scratch/authority-measure-500k 500000 64
```

The authority Node retains its storage backend through shared Worker ownership. `NodeOptions::scheduling.async_storage` enables the complete runtime IO profile; the host claims `node.worker().take_io_operation()` and drives original completion on the same owner executor. The synchronous profile remains the default while production transport integration is unfinished. The same Node and three-node application tests run in both profiles, covering checkpoint images, replay, partitions, snapshot installation and recovery. All 32 native Detent suites pass (43.78 seconds); four Node/cluster profile suites pass under ASan/UBSan (15.18 seconds). Sanitizer compilation reports warnings in the existing cpp-btree dependency. This qualifies the application adapter, not authenticated multi-process completion storage.

## Completion-based service execution

The single-voter authority owner runs one Asio event loop. On macOS and FreeBSD it drives the full owned Worker storage profile through the BSD completion backend, retaining the Node and original operation through shutdown reaping. Native payload reads/writes and fallback namespace/full-flush operations are reported separately in health. Bootstrap, recovery and inventory remain synchronous before admission. Linux retains synchronous storage until its native backend is implemented; FreeBSD remains unqualified on its target OS.

Existing recording or embedded ResponseWriters that cannot defer use the bounded synchronous compatibility path. Production Asio request handling returns after enqueueing and completes through an owned callback. Admission accounting and immutable credential storage survive Service retirement while a socket operation retains them. This activates completion-based storage in the local authority profile; it does not add peer TLS, replicated availability or agent distribution.

For this local development integration, configure Detent with sibling source overrides for `HTTP_LIB`, `REACTOR` and `CLUSTER`: `-DFETCHCONTENT_SOURCE_DIR_HTTP_LIB=$PWD/../Kronuz/http -DFETCHCONTENT_SOURCE_DIR_REACTOR=$PWD/../Kronuz/reactor -DFETCHCONTENT_SOURCE_DIR_CLUSTER=$PWD/../Kronuz/cluster`. These revisions remain local until publishing is authorized; a remote `main` fetch does not include them.
