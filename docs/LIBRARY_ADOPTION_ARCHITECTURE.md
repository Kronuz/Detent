# Validated Architectural Blueprint: latest-library adoption

**Date:** 2026-10-17
**Status:** Design accepted. The preliminary 37-workload baseline is captured; grouped integration and paired qualification are active. No dependency pin has changed on main in this pass.

Adopt the latest published Kronuz dependencies in coherent groups, comparing identical application sources before and after. Audit the wider library catalog for reusable mechanisms, but keep pin upgrades separate from changes to storage, wire formats and concurrency ownership. The [refreshed candidate inventory](../benchmarks/library-adoption-candidates-20261017-r2.json) freezes published heads and records local divergence; it supersedes the older compatibility snapshot in [the coordination ledger](DEPENDENCY_UPGRADES.md).

## Freeze and adoption order

Finish the persistent transport component's platform checks, then preserve its application tree, actual dependency sources, build settings and binaries. The existing frozen configuration overrides cpp-btree with `.scratch/stage6-btree/after`; archive and hash that directory as well as the declared revision. Both benchmark arms must use the same application source, compiler, codec linkage and corpus. Exercise each library revision's defaults; record any required nondefault settings and the evidence for them. Capture every resolved dependency rather than relying on declarations alone.

| Group | Scope | Qualification boundary |
| --- | --- | --- |
| A | Documentation/CI-only revisions and additive APIs | Implementation/API diff, build profiles and footprint |
| B | SHA-256, varint and hashes | Valid digest/wire fixtures, malformed inputs, sanitizers and codec workloads |
| C | Scheduler, stash, threadpool and random | One shared random target, timer behavior, original completion and shutdown |
| D | Compressors | Preserve enabled backends and actual codec versions; qualify helper adoption separately |
| E | Completion | Retain individual-sync, existing mappings and the current storage backend |
| F | Journal and consensus | Coherent closure, homogeneous DTC2 restart, recovery and rollback |
| G | Optional mechanisms | Demonstrated bottleneck and a separate ownership/durability design |

Most leaf updates are documentation or CI changes. Published HTTP and reactor have no new production implementation relative to the qualified pins; they do not replace Detent's persistent client, offload-limit mitigation or typed header-pressure handling. Keep `random_lib` available before scheduler consumers. Keep all existing compressor backends enabled initially, with the same installed or pinned zstd/LZ4/zlib implementations in both comparison arms.

## Reuse audit

| Detent mechanism | Available library capability | Decision |
| --- | --- | --- |
| Three HTTP clients and the persistent cache | HTTP remains a server framework | Retain current clients; a shared client is an extraction opportunity |
| TLS setup | `reactor::tls` | Already shared |
| Heap ledger and retirement | Journal admission and completion allocators | Shared primitive opportunity; preserve concurrent RAII refund and retirement contracts |
| Disk ledger | Journal admission | Retain explicit proved-settlement wrapper; destructor refund would be unsafe |
| Completion scheduler | Threadpool TaskGroup | Different ownership contracts; pool tasks do not replace original I/O settlement |
| Deterministic byte/receipt budgets | Task time slices | Complementary mechanisms |
| Segment mappings and leases | Completion mappings and advisory residency | No replacement preserves accounting, ownership, validation and retirement yet |
| Streaming MessagePack lexer | MessagePack DOM/COW | Retain bounded streaming validation; consider extracting the narrow lexer |
| Immutable persistent maps | cpp-btree and caches | Different snapshot/lifetime semantics |
| SHA-256 commitments | Accelerated CRC-32C | Preserve committed formats; CRC is not a substitute |

Residency currently supplies advisory `MADV_WILLNEED`, which can still leave page faults. Completion mappings allocate controls outside Detent's ledger. Neither supplies the complete generation lease and retirement contract. The bounded zstd string helper rejects frames without content size and does not preserve the managed decoder's admitted output/workspace model. The legacy HTTP client still needs independent header, wire and gzip expansion bounds.

Three concrete consolidations have matching source contracts. Apply them separately after the same-application dependency comparison, then rerun their focused regressions:

1. `PagedSegmentMarks::hash` in `src/detent_local/paged_segment_marks.cc` duplicates `fnv1ah64::hash` from `hashes.hh`, including unsigned-byte conversion, offset and prime. Link the existing hashes target explicitly on local publication; preserve the reader-only dependency boundary.
2. Authority `identity(string_view)` and delivery `identity(Value)` duplicate `parse_identity_hex<16>` in `include/detent/identity.hh`. Preserve exact length, lowercase-only and nonzero rules, along with domain diagnostics where callers depend on them.
3. Certificate fingerprint parsing in `src/detent_authority/service.cc` can use the already linked character classification library's `hexdigit` and `hexdec` after validating the 64-character length. Its mixed-case acceptance must not leak into canonical identity parsing.

Header-name normalization in `ResponseDecoder` can also use the already linked `chars::tolower`, preserving ASCII semantics without importing HTTP server helpers. These helper changes are prepared in a separate reuse checkout and remain uncompiled during the benchmark hold.

## Known library boundaries

Published completion `5251cf9` now includes the per-device macOS synchronization correction. Earlier candidate `e6c28aa` did not. Qualify the corrected published implementation directly, preserving the existing backend during the pin comparison. Shared-log or batched-I/O adoption remains a separate measured mechanism change, without a parallel old implementation.

Published threadpool `628679a` defines restart after completion and fixes the earlier one-shot promise/future issue. Published compressors `ed590c8` tests supported backend subsets and rejects all-disabled configuration. Exercise these corrected APIs rather than retaining workaround behavior. Original I/O settlement still differs from TaskGroup ownership, so pin adoption alone does not justify replacing it.

## Measurements and acceptance

Use five fresh-process samples for short benchmarks and three for longer workloads, alternating old/new order after both builds exist. Retain preliminary old-library samples before changing pins, then run the paired comparison. Keep comparison runs separate from compilation and unrelated load. Record raw samples and uncertainty; a noisy result crossing a gate remains unresolved.

| Dimension | Workload |
| --- | --- |
| Footprint | Release reader-only, legacy, authority, relay and managed-agent profiles; stripped/unstripped bytes, text/data, linkage and startup RSS |
| Read path | 1/4/8 threads, 2M operations per thread; shared-memory reads separately |
| Authority | 100k/500k definitions, uniform/hot namespaces, normal/override-heavy |
| Durable storage | 100k/500k definitions, 16/64/256 producers; commit, checkpoint, recovery, latency and physical bytes |
| Codec | 1k/100k/500k definitions, raw/zstd and delta decode; CPU, wire bytes, workspace and malformed-input checks |
| Local images | 100k/500k image creation, paged recovery and collection; cold/warm work and coexistence |
| Ingestion | 500k definitions, 16/64/256 producers; calibrate, then matched offered rates at 80/100/125% of the old plateau |
| Fleet | 500/2k/10k sessions, raw/zstd, cold/warm, 128 active exchanges, fixed owners; 10 minutes per admitted level |
| Mixed/failure | 30-minute 500k/256-producer workload, leader death, minority loss, restart and nonempty catch-up |

The standalone durable benchmark currently supports 16/64 producers; exercise 256 through the real HTTP harness without changing the benchmark during A/B. The existing codec executable takes `json` or `messagepack` and reports raw/zstd measurements internally. Record these exact interfaces in the workload manifest.

Correctness gates permit no unexplained oracle mismatch, acknowledged-write loss, fabricated settlement, sanitizer error or undrained original. Investigate reproducible CPU/work or throughput regressions above 5%, p99 or longest-turn regressions above 10%, and unexplained peak-memory growth above 5%. Settled resources must return to their retained-owner baseline. Flag executable growth above the greater of 1% or 64 KiB. Preserve existing valid wire/disk fixtures and account for any amplification.

Compare CPU per committed operation and delivered GiB, not total CPU across differing closed-loop work counts. Translate CPU seconds per elapsed second into core demand; dollar estimates require a stated price, and power claims require energy measurements. The 10k fleet preset contains 1,000 records and transport probes, so it does not establish 10k durable-agent capacity or replace the separate 500k workload.

## Consensus transition and compatibility

Latest consensus uses DTC2. Adopt it with the matching journal and completion closure, retaining fixed voters and the named-file backend. Start from library defaults for timing, pipeline, snapshot windows and batching; do not preserve older values through compatibility overrides. Freeze the exact peer protocol contract; future incompatible frames require a new version or a designed transition.

Source inspection found a default timing change. Synthetic callers must derive their election delay from the configured `Timing`, rather than assuming 100 ms. Apply that caller correction to both comparison trees and leave `NodeOptions` on library defaults. Existing production overrides equal the latest defaults; remove them after pin adoption as a separate cleanup. A default change is not evidence of regression. Retain a nondefault setting only when qualification establishes a reason, and record it in the shared ledger for library-level evaluation.

Latest recovery uses library ceilings to support lowered-limit reopening, but its incremental encoder still checks some retained payloads against operating limits. Resolve this in the library through one retained-state resource policy and shared validation used by decoding, replay, encoding and Core recovery. Keep historical group limits distinct from allocation bounds. Check descriptor lengths and decoded counts before allocation, and aggregate retained bytes before growth. Do not add a legacy recovery implementation or a Detent-only decoder. Evaluate the library's common default policy before choosing a custom host bound; record any demonstrated regression and its rationale in the shared ledger. Group F remains unqualified until these consistency and resource tests pass.

The configured `maximum_size(limits)` reservation calls remain correct. Snapshot windows still default to one, follower batching remains disabled and the named-file Worker constructor does not enable shared logs. Do not expose dynamic membership: Detent's adapter handles application commands and NoOp, not membership changes. Later telemetry integration should add `peer_queued_data` alongside the new control/data queue split; keep it outside the identical-application pin comparison.

Use coordinated restart: stop producer admission, reconcile receipts, checkpoint a quorum, stop all voters, preserve every store, restart all voters on the new revision, verify semantics/receipts/quorum, then resume admission. Mixed old/new peers must fail closed in both directions. Cover slow snapshots, delayed final installation, bounded windows, control starvation and append drops.

Before new admission, rollback restores the preserved stores and old binaries together. After new writes, prove old-binary recovery against new-produced stores; restoring stale backups would lose acknowledged work. Shared logs and membership formats remain disabled and have separate rollback gates.

Run application checks on macOS, Linux and FreeBSD, ASan/UBSan and fully instrumented TSan. Record native versus fallback I/O separately. Shared runtime changes also require the current Xapiand full-build and replication/search gates with exact profiles. Preserve its valid legacy varint bytes and thread-policy interfaces; adopting an additive LEB128 API would be a format change.
