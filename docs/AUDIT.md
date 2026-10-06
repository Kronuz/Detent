# Source audit, October 2026

This review covers the owned C++ client, JSON schema and patch handling, agent lifecycle and shared-memory layout, HTTP polling, distributor persistence and HTTP routes, Raft adapter, Python and Rust bindings, and build/test wiring. Dependency implementations are inspected where their contracts affect owned code; this is not a full audit of every fetched dependency.

Changes are recorded here with their triggering conditions and validation. Each audit fix is committed separately. Native tests run on macOS; Linux and FreeBSD service templates require platform integration testing.

## Deployment limits

HTTP and UDP cluster traffic have no built-in authentication or TLS. Use an access-controlled network and an authenticated TLS proxy for remote publishers. The cluster adapter relies on a volatile Raft implementation and restart silence; it is not a durable production Raft log. See `src/detentd/CLUSTERING.md`.

The shared-memory design currently uses a seqlock over non-atomic payload fields. Its formal C++ data-race limitation is documented in `shm_format.hh`. Stress-test success alone does not establish language-level race freedom. No performance claim in this review is based solely on code inspection.

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

The opt-in authority adds a separate durable single-voter store and credential-bound loopback API. The legacy deployment limits above still apply to the legacy HTTP/multicast distributor. [Authority documentation](AUTHORITY_ARCHITECTURE.md) records transaction semantics, resource bounds, recovery tests and migration. Multi-node authenticated sockets, binary public distribution and immutable agent views remain pending components.


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
