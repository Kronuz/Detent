# Source audit, October 2026

This review covers the owned C++ client, JSON schema and patch handling, agent lifecycle and shared-memory layout, HTTP polling, distributor persistence and HTTP routes, Raft adapter, Python and Rust bindings, and build/test wiring. Dependency implementations are inspected where their contracts affect owned code; this is not a full audit of every fetched dependency.

This is an ongoing audit: entries record their triggering conditions and qualification at the time of each correction. Each audit fix is committed separately. Current distribution/resource checks also run on Linux and FreeBSD; final service deployment and fleet qualification remain open. The current stage and acceptance boundaries are recorded in [implementation status](IMPLEMENTATION.md).

## Deployment limits

The legacy HTTP/UDP cluster profile has no built-in authentication or TLS and uses volatile Raft state. It requires an access-controlled network and an authenticated TLS proxy for remote publishers. See [legacy clustering](../src/detentd/CLUSTERING.md). The additive authority profile provides authenticated transport and durable consensus; its [component review](AUTHORITY_COMPONENT_REVIEW.md) and [readiness gates](AUTHORITY_READINESS.md) describe its qualification and remaining production limits.

The production agent’s legacy shared-memory path uses a seqlock over non-atomic payload fields. Its formal C++ data-race limitation is documented in `shm_format.hh`. The new [immutable local views](LOCAL_IMAGE_FORMAT.md) remain under Stage 4 integration; their library qualification does not replace that production path. Stress-test success alone does not establish language-level race freedom. No performance claim in this review is based solely on code inspection.

## Fixed: delta gathering crossed history incarnations

A delta encoder admitted against one history incarnation could gather its first transaction, then continue from repopulated history after a same-context reset. The old coverage check saw a complete revision range again and did not detect that the encoder mixed incarnations. The regression uses `ns:a` at revisions 1–3 with values 11–13, resets history to revision 0 with unchanged context, then repopulates values 101–103. It fails against the preceding encoder at `0c5e336` and rejects the update before any chunk escapes after the correction. This proves the library condition; it does not establish that the production authority performs a rollback without an epoch change.

Delta gathering now binds one owner/reset-generation handoff, checks it on every gather turn and after the final transaction, and distinguishes typed history loss from representation limits and storage pressure. Complete gathered immutable inputs remain usable across subsequent eviction. Existing constructor calls remain source-compatible through an optional parameter; consumers of the internal authority-export C++ interface must rebuild.

The two delta/delivery export fixtures pass under ASan/UBSan (1.13 seconds) and fully instrumented TSan (2.90 seconds), on Linux (0.18 seconds) and FreeBSD (0.15 seconds). The old-encoder reproduction is retained locally in `.scratch/history-reset-before/result.log`.

## Fixed: reader argument errors launched the agent

`detent get ns:missing --bogus` forwarded the unknown option to a child daemon, printing both an argument error and a misleading startup failure. Adding `--default -4` suppressed the failure and returned success with `-4`. Reader argument validation now precedes configuration and startup, rejects unknown options, missing values and extra positional arguments, and preserves negative defaults. The CLI regression verifies exit 1, empty stdout, no startup diagnostic and no runtime artifacts for invalid invocations; normal startup, stopped-agent restart and configuration discovery also pass.

The CLI fixture passes natively (7.06 seconds), under ASan/UBSan (10.46 seconds), on Linux (6.25 seconds) and FreeBSD (6.13 seconds). The real managed authority/agent/reader process fixture and certificate setup pass natively (4.86 seconds), and all 66 website pages build.

## Fixed: scope header failed after OpenSSL headers

Including the managed TLS client before `detent/scope.hh` exposed OpenSSL's global `SHA256` function, hiding the hash class name and failing compilation. The scope helper now explicitly selects the class, preserving its hash bytes and avoiding include-order dependence. The failure was reproduced while building managed configuration; that build, its real TLS/configuration test and the existing authority scope regression pass after the correction.

## Fixed: JSON string truncation and encoding validation

An escaped NUL in a knob name (`a:b\u0000hidden`) was truncated to `a:b` and accepted. The new regression failed on the preceding implementation. Parsers now use explicit JSON string lengths for field names, type names, knob names, and group keys, validate UTF-8, and reject raw NUL bytes in JSON text. Serializers preserve complete opaque group keys, including escaped NULs. Schema, patch, and JSON-string regression tests pass.

## Fixed: invalid client poll intervals

The public C++ options (also used by bindings) accepted zero and negative poll intervals, allowing a background source to loop without waiting. Construction now rejects them before starting an updater. The file-source regression checks both values with a configured polling source.

## Fixed: unsafe shared-memory file mapping

A reader mapped the full snapshot capacity without checking file size. An empty or truncated runtime file could produce SIGBUS on access. Readers now require a regular file large enough for the complete mapping and reject symlinks. Missing files remain an ordinary startup condition; other open errors are reported. Override offsets must also satisfy record alignment before dereferencing. Regressions cover empty, short, symlink, and missing files, alongside the normal agent publish/read test.

## Fixed: snapshot file allocation limits

Defaults and file-source reads enforced the 16 MiB limit only after loading the whole file, and distributor startup loaded persisted files without a limit. A shared bounded reader now checks each chunk before appending and reports read errors. Distributor startup rejects unreadable or oversized existing state files. The file-source regression covers exact and exceeded limits; distributor persistence tests cover normal reload behavior.

## Group rollout extension

Group overrides now accept either a forced boolean or an object containing only an integer `rollout_pct` from 0 to 100. Group percentages use the same knob/user hash as the global rollout, preserving nested cohorts. Missing hashes use the knob default, and unknown groups retain global evaluation. The JSON schema rejects malformed group definitions rather than ignoring fields. Snapshot and PATCH serialization preserve the new definitions.

Shared-memory layout version 2 distinguishes forced values from percentages. Upgrade readers and restart agents together. C++ `BoolDef::overrides` values are now `GroupOverride` variants of `bool` and `GroupRollout`; code inspecting values directly must use the variant API. Language binding read APIs retain their existing hash and switch parameters.

Validation for this extension: the full C++ build succeeds and all 13 tests that do not require sockets pass. The core and shared-memory readers agree across 1,000 host IDs in each of seven groups, including 0% and 100% bounds. Quick Start JSON parses, and its group reads and missing-knob fallback pass through a real local agent using the file source. The earlier socket restrictions are now removed. All 23 native tests, including HTTP and cluster tests, pass, and the real-process Raft harness passes all seven scenarios with five chaos rounds.

## Fixed: silent failure to publish initial defaults

Valid defaults can fit the 16 MiB JSON input limit while exceeding the agent's 4 MiB shared-memory payload capacity. Startup previously ignored the failed initial publish and reported readiness with an empty or stale mapping. Construction now fails with an explicit capacity error; background startup forwards that failure to the invoking reader instead of reporting ready. Live updates that exceed capacity still preserve the last published snapshot and emit an error.

A regression uses one valid boolean knob with 40,000 group overrides. It fails against the previous implementation and passes after the fix. All 23 native tests pass, including normal CLI auto-start, HTTP updates, shared-memory concurrency, and the capacity regression.

## Fixed: logging traversed snapshots during idle polling

The first logging integration called `Client::health()` every 200 ms. That API counts boolean and integer knobs by traversing the snapshot twice, even when debug logging is disabled. Agent logging now reads the existing atomic source-connected, snapshot-swap, and refresh-failure counters instead. Full health queries retain their existing behavior. This removes the snapshot traversal from routine logging; no measured CPU or throughput improvement is claimed.

## Fixed: RapidJSON compiler incompatibility

Clang 23 rejects RapidJSON 1.1.0's `GenericStringRef` assignment operator because it assigns to const members, even when the only consumer code includes `document.h`. Detent now pins upstream `24b5e7a8b27f42fa16b96fc70aade9106cf7102f`. The minimal reproducer fails with 1.1.0 and passes with the pin; strict JSON regression tests retain their behavior. This qualification does not claim a reproduced Detent parser vulnerability.

## Fixed: SHA-256 padding pointer arithmetic

The authority sanitizer run found an out-of-bounds intermediate pointer in the shared SHA-256 dependency: `extra + paddedLength - BlockSize` first advances beyond the second block before subtracting. Sibling sha256 commit `8acc96b` changes this to `extra + (paddedLength - BlockSize)`. The original fails UBSan for the 56-byte published vector. The corrected library passes ASan/UBSan with published vectors and added padding-boundary cases, including streamed input and repeated reads. Digest bytes and public interfaces are unchanged; Xapiand can reconcile this as the same one-line correction. Local qualification selects the sibling source through FetchContent; the dependency commit has not been pushed.

## Fixed: transitive exception declarations

Clang 23 with libc++ exposes missing declarations for `std::exception_ptr` and `std::current_exception()` in the HTTP polling source. The source now directly includes `<exception>`; native and sanitizer daemon builds pass.

## Durable authority scope

The opt-in authority adds a separate durable single-voter store and credential-bound loopback API. The legacy deployment limits above still apply to the legacy HTTP/multicast distributor. [Authority documentation](AUTHORITY_ARCHITECTURE.md) records transaction semantics, resource bounds, recovery tests and migration. Multi-node authenticated transport and source retirement have a [consolidated source review](AUTHORITY_COMPONENT_REVIEW.md), with the remaining production pressure and telemetry gates tracked in [readiness](AUTHORITY_READINESS.md). The binary codec and immutable image/publication modules are implemented and under component qualification; actual agent integration and fleet distribution remain pending.


### Fresh-build filesystem fixtures

A clean cluster sanitizer build reproduced a pre-existing missing `.scratch/` parent: the POSIX journal fixture failed before exercising storage. Cluster `8b74ded` creates that build-local parent during root and standalone test configuration. A fresh standalone configure/build/CTest run passes without manual directory setup.

### Append submission after fencing

The existing append operation checked in-flight/terminal state before submission but omitted the owner's fence. A raw owned append could therefore accept a new primitive after `fence_storage()`, even though its later completion fenced. The same missing guard existed before artifact integration. A real POSIX reproduction accepted the write and grew the file from 28 to 52 bytes. Cluster `da549c3` rejects new submission before IO, including between reaped steps, and preserves matching reaping for the original in-flight request. The reproduction now leaves 28 bytes; native and ASan/UBSan journal regressions pass.

### Reclamation before completed inventory

The artifact overlap guard introduced in cluster `86de3d8` called `mutation_ready()` from `Store::reclaim_step()`, inadvertently requiring completed inventory/admission for the existing pre-census cleanup path. Cluster `5981f04` checks the overlap gate independently, restoring orphan removal and invalidation of a partial census. A common-API POSIX reproduction removes one orphan with `8372e29`, fails with the affected code, and removes it after correction. Two new regressions fail before the correction; all 162,422 journal checks pass afterward natively (1.72 seconds) and under ASan/UBSan (2.48 seconds). The fresh census accounts exactly for surviving files. This is an introduced regression, not a pre-existing storage defect.

### TCP startup readiness

A real HTTPS rerun exposed that TCP `start()` returned before bind/listen completed. Reactor `6f68823` waits for every selected listener and propagates startup exceptions after original accepts and sessions retire. Immediate connections pass in both bind modes, and an occupied identical endpoint now fails startup explicitly. Native and ASan/UBSan runtime suites pass; the real HTTPS fixture passes twenty consecutive repetitions. Constructor and Session APIs are preserved. Callers that previously expected asynchronous bind failure must handle startup exceptions.

### Linux transport and fixture portability

Linux GCC exposed an undeclared algorithm-header dependency in peer hello decoding. Detent `add5f65` includes the declarations used by `std::copy_n`; the affected targets compile and link. The TLS peer fixture inherited group-writable directories under the guest's umask, which PosixIO correctly rejected. Detent `34395fa` creates explicitly private stores without relaxing storage ownership checks. The daemon smoke fixture also required undeclared ripgrep; `4a0f3d8` uses portable extended grep. Both formerly failing fixtures and certificate setup pass on Linux in 5.02 seconds. These were portability gaps in the new transport and its fixtures, not io_uring storage failures.

### Nested staging construction with Clang/libstdc++

Cluster `a855e4f` gives the private Worker staging Chunk an explicit zero-initializing constructor. An isolated Clang 18/libstdc++ 13 reproduction fails optional emplacement with the previous default member initializers while GCC 13 succeeds; both compile after correction. Linux authority sanitizer compilation and the macOS Worker suite pass. Fields, layout, initialization values and public APIs remain unchanged.

### Default build compatibility with the preceding HTTP API

Authority integration introduced an unconditional reference to HTTP's new TLS connection-factory type and five-argument constructor, even when authority was disabled. Compiling the default main against HTTP `c3193b9` reproduces four errors before correction. The factory and constructor extension are now confined to the authority build; a complete default daemon builds against that preceding HTTP revision and serves help. An authority request in a build without that feature now fails before attempting status probes. This restores the previous default dependency API rather than requiring unpublished TLS additions for all builds.

## Fixed: HTTP one-shot socket double close

The GET, PUT and PATCH helpers closed their socket before parsing the response, then closed that descriptor again when parsing threw. Another thread could reuse the descriptor between those calls, allowing the second close to destroy an unrelated resource. Request construction also happened before the exception cleanup block.

A scoped socket owner now covers request construction, transfer and response parsing, closing exactly once during scope exit. The regression returns malformed responses over real TCP and forces descriptor reuse during exception allocation. It fails against the preceding source and passes after correction for all three helpers. The agent HTTP and distributor integration gates pass; the socket regression passes on macOS, Linux and FreeBSD and under macOS ASan/UBSan.

### Validate HTTP response lengths before accepting state

The client used `atoll` for `Content-Length`, accepted trailing junk and conflicting duplicate values, ignored `Transfer-Encoding`, and accepted truncated one-shot bodies. Persistent requests treated missing lengths as zero and silently truncated excess bytes already read. These paths could return incomplete state or leave a connection out of alignment with the next response.

Length parsing now requires a complete unsigned decimal value, rejects conflicting duplicates, and verifies the received body against the declared length. Identical duplicate lengths remain accepted. Unsupported transfer coding is rejected explicitly. Responses without a declared length are read through connection closure and the connection is discarded. Bodyless 204/304 responses bypass decompression, including a 304 carrying representation metadata. Failure on the second persistent attempt also disconnects the socket.

The real TCP fixture exercises malformed and overflowed lengths, duplicate lengths, truncated/excess bodies, chunked coding, close-delimited responses, and bodyless responses in both one-shot and persistent clients. It fails against the previous source at the first invalid length and passes on macOS, Linux and FreeBSD, and under macOS ASan/UBSan. These checks do not address aggregate HTTP body/decompression limits or asynchronous connection establishment, which remain audit work.

### Release settled publication payloads

A rejected image publication retained its candidate bytes and completed preparation job until the publisher was destroyed or another candidate began. With charged buffers this also held quota after the original operations had settled. A failure at the final selection directory barrier retained the same bytes while fenced.

The failure path now releases settled jobs and candidate bytes. It records selection uncertainty independently before dropping the completed job, preserving the fence and uncertainty through close. It never releases an in-flight original: `advance()` still requires authentic settlement. The regression holds the publisher alive after both an image-write failure and a final selection-barrier failure, then verifies physical buffer release and zero retained charge. It fails against the preceding source and passes after correction on macOS, Linux and FreeBSD, and under macOS ASan/UBSan. Disk artifacts remain protected pending reconciliation and generation cleanup.

### Allocate the directory owner before opening its descriptor

Generation recovery evaluated `open()` before `make_shared<File>` allocated its owner. If that allocation threw, no object owned the successful directory descriptor, so it leaked. Recovery now constructs the empty scoped owner first and installs the descriptor afterward.

The regression injects failure into the first owner allocation and compares open descriptor counts. It reproduces the leak against the preceding source and passes after correction on macOS, Linux and FreeBSD and under macOS ASan/UBSan. It also checks missing-selection cleanup; native view and publication suites remain passing.

### Reject closed retirement admission without accessing its runtime

An already-drained domain could outlive its joined runtime, but attempting allocation through that closed domain still signalled the freed runtime while undoing admission. ASan reproduces the resulting heap-use-after-free. Admission now reserves a resident directly in the same atomic state that records closure. Closed and capacity-rejected attempts never touch the runtime; accepted construction keeps its resident until publication or rollback. Rollback retains a separate handoff until its final runtime access has completed.

The regression retains a closed domain after runtime destruction and verifies safe rejection. Additional schedules close a domain while successful and failing constructors are blocked, retaining drainage until their genuine settlement. The focused suite passes on macOS, Linux and FreeBSD and under macOS ASan/UBSan and TSan. This fixes domain admission ownership; mapping and disk admission remain separate work.

### Allow scope cleanup to progress with small byte budgets

Snapshot and delta decoder disposal required an entire scope entry to fit in one turn’s byte budget. A valid long namespace therefore prevented cleanup from ever completing when the scheduler repeatedly supplied smaller budgets. Disposal now accumulates byte credit across turns and consumes the row token only when that entry’s cleanup charge is complete. Zero byte or token budgets cannot remove an uncharged entry.

Both regressions reproduce the stall against the preceding source by disposing a maximum-length namespace with one-byte turns. They pass after correction on macOS, Linux and FreeBSD and under macOS ASan/UBSan. The change preserves scope validation, wire formats and the existing page-retirement mechanism.

### Reject malformed HTTP status lines

Response parsing converted the status token with `atoi` and never checked the protocol version. A response such as `HTTP/1.1 200junk OK` or `garbage 200 OK` was therefore accepted as status 200. The parser now requires a supported HTTP/1.0 or HTTP/1.1 version, a three-digit status in the supported response classes, the field separators, and a reason phrase without forbidden control bytes. An empty reason phrase with its separator remains accepted.

The real TCP fixture covers malformed lengths and signs, invalid status classes, missing separators, unsupported versions and control bytes in both one-shot and persistent clients. It fails against the preceding source and passes after correction natively and under macOS ASan/UBSan. Native agent HTTP and distributor integration gates also pass. This correction leaves header grammar, body/decompression quotas and asynchronous connection establishment as separate audit work.

The expanded HTTP status and socket-ownership suites also pass on Linux and FreeBSD after restoring the existing qualification VM. No wire-body or status is accepted from the malformed fixtures.

### Reject malformed HTTP header fields

The response parser silently ignored lines without a colon and trimmed whitespace from field names. It therefore accepted malformed responses containing `Broken-Header` or `Content-Length : 3`, as well as folded fields and forbidden control bytes in unknown field values. Both client paths used that parser before returning state.

Every header line now requires a nonempty token field name immediately followed by its colon. Field values reject forbidden control bytes before trimming; spaces and tabs remain valid value whitespace. Unknown fields still participate in structural validation. The parser explicitly rejects obsolete folded lines rather than silently dropping them.

The real TCP regression fails against the preceding source at its first malformed field. Ten invalid-field cases then pass after correction in both one-shot and persistent clients, alongside a valid field name using the supported punctuation characters. Native agent HTTP, distributor integration and socket/framing gates pass (3.83 seconds); the two HTTP suites also pass on Linux (0.93 seconds), FreeBSD (0.95 seconds) and under macOS ASan/UBSan (1.77 seconds). Media-type/content-coding interpretation, aggregate body/decompression limits and asynchronous connection establishment remain separate audit work.

### Match patch media types exactly

The HTTP client selected patch application using a prefix match on `application/merge-patch`. Unrelated media types such as `application/merge-patch+json-seq` therefore set the patch flag, while a valid case-varied `Application/Merge-Patch+Json` did not. That flag controls whether source bytes replace or merge into existing state.

The parser now compares the complete media type case-insensitively after separating optional parameters. Only `application/merge-patch+json` selects patch application. The TCP fixture checks four unrelated types and three valid variants in both client paths; it fails against the preceding source and passes after correction. Six native HTTP/agent/distributor/diff gates pass (5.43 seconds), and the two HTTP suites pass on Linux (1.04 seconds), FreeBSD (1.11 seconds) and under macOS ASan/UBSan (2.09 seconds). Content-coding interpretation and response size limits remain separate audit work.

### Reject unsupported HTTP content codings

The client returned bodies unchanged when their Content-Encoding was unknown or chained, and repeated fields silently replaced earlier codings. Encoded response bytes could therefore be returned as if they were identity state. Case-varied supported codings also bypassed decoding.

Body decoding now accepts a single identity, gzip or zstd coding case-insensitively and rejects unsupported, empty or repeated/chained encoding fields. Bodyless responses still ignore representation coding metadata and never attempt decompression. The TCP regression fails against the preceding source on an unsupported coding; the expanded cases cover chains, repeated fields, identity casing, actual uppercase GZIP decoding, and a 304 with unsupported representation metadata in both client paths.

Six native HTTP/agent/distributor/diff gates pass (5.81 seconds). The two HTTP suites also pass on Linux (1.35 seconds), FreeBSD (1.34 seconds) and under macOS ASan/UBSan (2.32 seconds). Aggregate header/body/decompression limits and asynchronous connection establishment remain audit work; rejecting unsupported coding does not establish those bounds.

## Cross-session publication completion replay

Publisher tokens used the candidate generation ID as their owner identity and restarted operation numbering in each publisher. Preparing the same generation in another session could therefore produce the same owner/operation/step token. A reproduced schedule completed an old lease directory barrier, submitted the corresponding new barrier, then delivered the old successful result; the preceding source accepted it before executing the new barrier.

`StorageSession` now retains the backend, stable owner lock, admission lease and allocator context, and assigns each session a fresh nonzero OS-entropy incarnation. Its operation IDs increase monotonically. Generation and persistent storage identities remain semantic identities rather than completion nonces. The regression now rejects the old result while retaining the real in-flight request, then consumes its authentic barrier and finishes publication. No image or journal format changes.

Publication tests pass on macOS (3.87 seconds), Linux (0.39 seconds), FreeBSD (0.40 seconds) and macOS ASan/UBSan (4.34 seconds). The full regression for this storage-session milestone and cluster `510c093` passes all 64 tests natively (188.14 seconds) and under macOS ASan/UBSan (215.01 seconds). This fixes token isolation and factors stable ownership; managed disk-session drainage and final native teardown on a worker remain lifecycle implementation work.

## Coroutine dispatch omitted newly added primitives

The additive caller-buffer/metadata primitives were routed by native queues, but the optional coroutine adapter still rejected every kind beyond the original twelve. This gap was introduced by the extension integration; the preceding twelve-kind adapter was correct for its original API. A real coroutine fixture fails at its first `NextInto` against that adapter.

The corrected driver shares native-kind validation and records new-kind timings in separate extension arrays, preserving the public legacy arrays and count. Invalid kinds fail before submission; driver availability remains usable afterward. The fixture covers all six appended kinds, actual metadata/lease/close effects, detached operation retention, metrics enabled/disabled and authentic IO failures. Seven standalone suites pass on macOS, Linux and FreeBSD and under ASan/UBSan, with a fully instrumented focused TSan run and native legacy journal coroutine compatibility. The [completion reference](../../Kronuz/cluster/completion/README.md#coroutine-extension-dispatch) records the optional telemetry footprint.

## Settle directory cursors before managed census readiness

The managed profile had explicit file closure but retained cursor cleanup only through wrapper destruction. That could execute `closedir` on the completion owner and could not report an authentic close outcome before inventory readiness. The appended `CloseCursor` primitive closes on the disk worker, consumes the POSIX handle once and leaves subsequent wrapper destruction inert. Managed capabilities now require cursor closure.

Eight standalone suites pass on macOS, Linux and FreeBSD and under ASan/UBSan. The focused fully instrumented TSan fixture checks worker identity, held originals across cancellation and facade retirement, stale results, close errors and reused native descriptors surviving later wrapper destruction. Journal compatibility and the coroutine extension sequence also pass. These gates qualify cursor settlement; complete inventory and session teardown remain separate application obligations.

## Keep subscription retirement proportional to owned scope storage

The session collector advanced its slot cursor after every partial scope-retirement slice. With 1,024 slots, a sole-owned large scope could require a complete slot scan between slices. It also charged the full scope for each shared reference, although dropping that reference does not destroy the scope graph.

The collector keeps its cursor at unpaid retirement work. It drops shared references on the authority owner and retains sole-owned scope storage until its remaining byte credit is paid. The regression closes a 1,000-name scope in a 1,024-slot table using one token and 8,192 bytes per turn; completion takes at most the scope capacity divided by the byte budget. The subscription and assembled native fixtures pass. HTTP request retirement uses the same ownership rule as part of the subscription integration.

## Yield ready reclamation during authority startup

Startup replay waited one millisecond after every bounded application turn, even when the recovered state's reclamation queue had immediate work. A native process sample showed the constructor waiting for readiness while the owner repeatedly slept between cleanup slices. The recovery loop now posts the next turn when state reclamation is pending; the existing timer remains when it is not. Each turn keeps its byte and receipt limits.

On the same Intel macOS host, the real recovered-authority 100,000-knob fixture fell from 88.66 to 6.54 seconds. The 500,000-knob fixture passes in 26.16 seconds; its baseline run was stopped before completion, so no paired improvement is claimed for that size. Measured constructor times after the change are 2.022 and 11.719 seconds. Both fixtures recover a production journal checkpoint, transfer a snapshot, publish exact feedback and complete 100 idle polls without additional quorum reads, exports or captures. These are end-to-end fixture times, not CPU, power or cost savings.

## Keep process tests from starting an unmanaged agent

The managed process fixture invoked `detent get` in user mode after deliberately stopping its agent. Those reads could auto-start another agent while the fixture changed credentials or authority identity. The test then had an untracked process using its storage, which invalidated later lifecycle checks.

Fixture reads now use `--system` with their explicit configuration, so only the agent started and tracked by the harness owns the managed store. The subscription process fixture passes on macOS, Linux and FreeBSD and under ASan/UBSan; the full native regression also runs the legacy polling fixture. Runtime auto-start behavior is unchanged.

## Remove concurrent subscription fixture diagnostics

The assembled ThreadSanitizer run passes 19 of 21 checks and identifies two races in global `std::cerr`: the scripted TLS server and the coordinator observer print progress concurrently. Both reports point to fixture output, with no production-state race reported. The progress prints are removed; assertions, protocol exchanges and ownership checks remain unchanged.

The affected subscription and forced-snapshot retry cases, including their certificate prerequisite, pass the corrected TSan rerun in 5.96 seconds. Combined with the original run, all 21 assembled cases have passing TSan evidence. The original full run takes 297.82 seconds and includes the 500,000-record authority and real managed-process cases. This is a targeted correction and rerun, not a second complete run.

## Preserve stable relay principal identity across policy epochs

Relay credential loading hashed the principal with its configured policy epoch, while authority and bundle bindings use a stable principal hash plus a separate policy epoch. Matching epoch-2 configurations passed parsing but failed immutable registry construction at startup. Credential loading now uses the stable principal identity; cursor scope and authorization still bind the actual policy epoch.

The regression loads matching epoch-2 source and credentials, builds the registry and authenticates/resolves the exact scope. The real two-bundle authority → relay → agents → reader fixture now uses epoch 2, including empty scope, recovery and grant revocation. These checks pass natively with certificate preparation in 8.04 seconds; the process fixture is part of the pending relay-host component. Logs are retained in `.scratch/relay-policy-epoch-tests.log`.
