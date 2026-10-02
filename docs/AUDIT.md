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
