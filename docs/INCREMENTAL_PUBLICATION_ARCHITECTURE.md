# Immutable incremental publication

**Date:** 2026-10-11
**Status:** Validated Architectural Blueprint, Stage 5C. The Lead Software Architect reviewed scalability, edge cases, dependencies/state, and security/scale at max reasoning effort. Stage 5B has component acceptance. Design GO covers this complete component, followed by one consolidated component review. Routine implementation commits do not require architectural approval.

## Boundary

Retain the selected-descriptor and public reader interfaces, profile-1 distribution encoders, publication sink and completion scheduler. Add a sealed paged-publication plan to the existing DiskStore state machine. Whole-image v1 remains supported; paged publication is an explicit configuration choice. Stage 6 retains sustained ingestion, fleet/bootstrap/reconnect storms, multi-host failure domains, audit/rollback and pinned dependency reproduction.

```text
Validated update + exact recovered base
                 ↓
Budgeted page builder → sealed publication plan
                 ↓
Existing DiskStore operations + CompletionPort
                 ↓
Durable root selection → validated recovered root
                 ↓
SDK views + publication sink + profile-1 export
```

## Format and logical identity

Add pure local codecs for immutable data/index pages, bounded segment packs and a small root manifest. Nodes have a 64 KiB ceiling and pack into segments of at most 1 MiB; there is no file per knob. Current supported definitions fit one leaf. Overflow framing remains reserved until a future semantic format explicitly supports larger definitions.

References bind segment identity, extent and checksum. Before allocating or mapping, bound depth, ranges, counts, aggregate extents and arithmetic. A canonical logical commitment covers definitions, key revisions and namespace watermarks independently of physical packing or update history. Physical checksums bind exact artifacts. Same-cursor equality cannot depend on page placement or compaction history.

The index uses a canonical compressed byte-radix trie. Watermark keys encode `W || namespace || NUL`; definition keys encode `K || namespace || NUL || local-name || NUL`. Names exclude NUL, and terminating both fields makes keys prefix-free. The checked decoder returns borrowed namespace/local-name views and rejects missing or extra terminators, unknown key types and invalid names. Branches have two to 256 children, ordered by unsigned byte labels; deletion collapses unary branches. A leaf suffix may be empty when its parent edge consumes the final terminator. Definition bodies length-frame opaque override keys, which may contain NUL.

The initial codec implementation checks versioned root/page framing, aligned segment extents, quotas, reserved fields, physical SHA-256 and logical page commitments. Branch commitments include the prefix and ordered child labels/logical hashes; they exclude segment identifiers, offsets and physical hashes. Native and ASan/UBSan tests reject corrupt framing and content, duplicate or unordered child labels, and undersized key buffers. These checks do not yet establish graph validity, budgeted construction, durable publication, reader integration or reclamation; v1 remains the operational format while those parts are implemented.

Definition payloads use a 24-byte header containing revision, type, default, optional rollout and override count. Each override carries a two-byte key length, one-byte type, one-byte value and its exact UTF-8 key bytes; embedded NUL is valid. Validation rejects noncanonical ordering, invalid percentages, nonzero reserved fields, trailing bytes and revisions exceeding the namespace watermark. Codecs use caller-owned storage. At the current limit of 256 overrides with 128-byte keys, the largest body is 33,816 bytes and fits a 64 KiB leaf including its header and key suffix. Overflow pages remain reserved and unsupported by the semantic format; enabling larger definitions requires explicit format/capability review rather than silently changing logical commitments.

The encoder exposes separate checked header and override writers for borrowed validated rows. Production builders can emit canonical bytes without constructing a legacy snapshot or owning override map; the `KnobDef` fixture/convenience overload delegates to the same writers.

## Builder and publication plan

Accept existing validated snapshots/deltas and an exact recovered base. Normalize repeated mutations with established transaction-order rules; replace affected leaves and index paths while retaining unchanged immutable subtrees. A move-only sealed plan owns new segment bytes, root descriptor, logical commitment, exact base descriptor/directory binding and validated reuse capabilities.

Gathering, sorting, hashing, zeroing and disposal obey work budgets. Progress without scoped state changes replaces root metadata only. Full snapshots and explicit migration may traverse the complete state.

The private page builder streams initialization, prefix/body copying and logical/physical hashing under the existing `ValidationBudget`. Output cannot escape before sealing; failure leaves it unpublished and worker disposal releases its admitted capacity. It retains and enforces caller-supplied page limits throughout construction. Native and ASan/UBSan tests use one-byte budgets, maximal fan-out, duplicate labels, smaller explicit prefix/byte ceilings and one-unit-short admission. The maximal branch fixture occupies 25,168 encoded bytes and admits 25,968 bytes including its controls and allocation headers; this is managed capacity, not process RSS. Segment packing is implemented separately; validated-snapshot and publication-plan integration remain pending.

The private canonical trie cursor consumes sorted typed keys with one lookahead and emits postorder leaf/branch events. Relative prefixes use the final parent depth, including when a shorter common prefix inserts an ancestor while closing a branch. It retains at most 202 open branch frames, with admitted child capacity per frame; empty and single-key inputs allocate no frame array. One-byte fixtures compare emitted commitments with an independent recursive grouping oracle and validate complete packed graphs, including namespace-prefix cases, empty leaf suffixes, maximum-length keys and 1,000 definitions. Cancellation is terminal and disposal releases frames under row budgets. Native and ASan/UBSan fixtures reject duplicate/unsorted/malformed keys and one-unit-short constructor or branch admission. Validated-snapshot integration remains pending.

## Durable selection

Add a plan overload to DiskStore publication while retaining ImageBuilder callers. Compose existing managed preparation/publication operations and CompletionPort; add neither a second IO engine nor a journal rewrite.

Reserve peak coexistence of new segments, ownership records and root selection before creation. Durable ownership precedes large payload writes. Synchronize every dependency before root selection and its directory barrier. Cancellation after possible selection requires authentic reconciliation before visibility or upstream feedback.

The checked 128-byte `DTSGOWN1` segment ownership record binds storage identity, segment identity, complete segment length and checksum, with its own integrity checksum. It identifies paged format 2 and rejects generation lease records, unsupported fields and extents outside 80 bytes to 1 MiB. Format bounds are independent of runtime quotas, so census can retain complete over-quota debt. Independent byte vectors and corruption checks qualify the codec; publication and reclamation still need to enforce its durable lifecycle and reachability rules.

## Recovery and readers

Give the private ReadView root a format-specific implementation, preserving public lookup/evaluation behavior and dependencies. Cold recovery validates the complete reachable graph. Warm recovery validates new pages and reuses previously validated immutable subtree capabilities bound to exact storage, directory and file identities plus checksums. Neither a descriptor nor a cursor alone authorizes reuse.

The private cold graph validator streams full-segment checksums before traversing referenced pages. It checks storage identities, distinct sorted segment identities, physical byte quotas, reconstructed typed keys, namespace watermarks, definition revisions and aggregate subtree/root counts. A temporary bitmap marks each eight-byte extent block and rejects repeated or overlapping physical cells. Its capacity is one byte per 64 segment bytes, approximately 4 MiB at the default 256 MiB physical ceiling, plus per-segment rounding. The traversal stack and key scratch have fixed bounds; namespace scratch is admitted to the declared namespace ceiling. Native and ASan/UBSan fixtures cover one-byte work steps, empty scopes, contextual corruption, padding-aware quotas and allocation failure during traversal. Actual paged recovery now binds opened immutable files, storage ownership and the selected root lease to this result before reader installation. Durable v2 publication remains disabled.

Private borrowed definition accessors read scalar fields lazily and share override/evaluation logic across v1 rows and canonical paged bodies. The existing release benchmark compares 100,000 integer lookups over 100,000- and 500,000-record synthetic corpora in three alternating runs: baseline/lazy medians are 72.696/62.537 ms and 118.111/117.202 ms respectively. Sample ranges overlap; these measurements do not establish a stable speedup. The [measurement record](../benchmarks/borrowed-definition-measurements.json) records the baseline and method. Native and sanitizer tests cover signed defaults, zero/full rollouts, embedded NUL/UTF-8 override keys and 256-entry override sets.

A complete private graph validation can create a paged `ReadView` in an explicitly supplied retirement domain. The root retains immutable registry ownership while traversal scratch is released; lookups reconstruct the typed key in bounded stack storage and binary-search branch labels and segment identities. Existing evaluation code handles boolean rollouts and switch overrides through the shared borrowed accessor. Fixtures cover view lifetime after validator/fixture release, failed root admission followed by successful retry, maximum-length names and maximum supported override bodies. Durable generation recovery and profile-1 export remain pending.

The complete native build and all 26 local-storage checks pass (140.61 seconds). The private storage/reader checks pass 11 ASan/UBSan tests in 3.56 seconds, with the final expanded graph/reader fixture passing separately in 0.31 seconds. TSan passes the graph/reader and existing flat-reader checks (2/2, 3.42 seconds). Linux and FreeBSD each pass six focused checks in 0.07/0.06 seconds. The minimal reader-only profile also builds, and the website builds 67 pages in 3.49 seconds. These qualify the current in-memory validation and read path; the complete-component acceptance below remains open.

Seven counterbalanced release trials check the existing flat lookup path after adding paged dispatch. Baseline/current medians for 100,000 integer lookups are 65.970/65.926 ms over 100,000 records and 109.604/108.757 ms over 500,000 records. The [raw measurement record](../benchmarks/paged-reader-dispatch-measurements.json) includes source hashes and baseline construction. Overlapping sample ranges support retaining this integration without claiming a speedup; paged throughput and complete physical storage footprint require separate component measurements.

Resolve required mappings and references before installing a root; hot lookup performs no filesystem work. Root leases remain separate from shared segment mappings. Reusing bytes cannot retain every obsolete ancestor generation indefinitely.

## Reclamation and compaction

Extend existing census, ownership proofs and reclamation operations for named segments and graph metadata. Protect selected, staged, uncertain and reader-leased roots. A segment's birth generation or a retired root's age cannot authorize reclamation of a dependency reachable from another protected root.

Use admitted traversal/mark capacity, bounded background work and selection rechecks before deletion. Preserve corrupt, foreign or unprovable artifacts and retain complete over-quota debt. Refund only after proved deletion, required directory barriers and handle closure.

Bounded compaction replaces sparse retained segment packs under the same admission and selection rules. Pinned readers retain old resources. Same-cursor compaction is local maintenance with verified logical equality; it creates neither an upstream feedback window nor a wire update.

## Compatibility and migration

Keep v1 codecs and publication callable. Upgrade readers before enabling v2 writers. Reuse the selected-descriptor shape if it unambiguously binds a versioned root manifest; version records only where their semantics require it.

An explicit migration capability binds the actual selected descriptor and directory, preserves Context/profile and proves same-cursor logical equality before selection. Existing readers retain their installed view on rejection. Cold recovery never substitutes an older generation for a corrupt selected root.

Coordinator configuration chooses the builder/plan while preserving recovered-head reconciliation and durable feedback. Adapt pinned-image traversal and profile-1 count/emission encoders to paged views. Keep physical-format identity separate from wire semantic identity. Maintenance refreshes recovered physical binding without authorizing a scope/policy transition or fabricating an update edge. Preserve sibling source APIs; document required consumer rebuilds.

## Resource and measurement contract

Charge changed-page buffers, indexes, validation capabilities, mapping controls and retirement debt to existing account classes. Bound total reachable state, mappings, segment/page counts and traversal workspace; a small root manifest cannot bypass state limits.

Ordinary sparse publication avoids both full-image rewriting and a full warm semantic scan. Record new/reused pages, written bytes, validation work, mappings, named disk debt, compaction work and pinned debt. Total measurements include background reclamation.

The Architect's format review confirms that 64 KiB is a node ceiling, not an allocation per leaf. Sparse work follows affected paths and node bytes, bounded by key length. Rebase and rehash relative prefixes during splits and unary collapse; a reused validation capability also binds its reconstructed key context. Profile-1 export uses an admitted lexical cursor or verified subtree rank counts, preserving reconstructed-key scratch across encoder steps rather than repeatedly scanning preceding records.

Reachable page extents and physical segment lengths have separate accounting. Mapping and disk ceilings charge each distinct segment's complete file length, including alignment gaps, unused cells and tail slack. Verify these totals against opened immutable segments before admission; removing a cell from a protected segment refunds no physical bytes.

## Concrete integration seams

The private segment builder packs sealed pages into admitted 1 MiB buffers, streams copies and full-segment hashes under byte budgets, and writes zero alignment padding. Sealing exposes only the initialized prefix and its distinct segment ownership record. Native and ASan/UBSan fixtures cover one-byte copying, page-owner disposal during copying, aligned references, full-pack admission boundaries, distinct publication ordinals and exact one-unit-short allocation rollback. Recovery, dependency publication and v2 reader activation are still pending.

The private page validator streams physical and logical hashes under byte budgets and checks ordered child framing under row budgets. It retains immutable input ownership and becomes terminal after corruption. Its result proves page framing and commitments; graph traversal must still prove reconstructed keys, namespace revisions, aggregate counts and extent uniqueness. Native and ASan/UBSan fixtures exercise one-byte steps, 256-child branches, 33,816-byte definition bodies, resealed physical corruption with invalid logical commitments and one-unit-short control admission.

1. Retain the 176-byte selected descriptor and use the generation `.image` for the 320-byte root. Prepare segment dependencies serially with `CloseAfterSeal`; preserve the existing final selected replacement and directory barrier. A Detent-owned composite retains the complete sealed plan through authentic settlement; one `PreparedImage` proves one artifact, not a dependency graph.
2. Keep root leases separate from segment mappings. A sorted admitted registry holds at most 4,096 segment identities, saved file identities/full lengths, mapping handles and live-cell/extent totals. Close segment descriptors after mapping and verification on the worker. Registry and mapping owners never retain ancestor read roots.
3. Share the registry for progress. Copy registry metadata under worker budgets for actual changes, making that cost explicit: `O(S + M*K)` metadata for `S` segments, `M` changed keys and bounded key length `K`. Record registry rows/bytes copied and total CPU; introduce COW registry blocks only if measurements justify them.
4. Derive contextual reuse capabilities by bounded navigation of a sealed prior root. Retain temporary changed-path evidence instead of a permanent proof object for every page. Bind each reused reference to its reconstructed prefix and exact storage/directory/file identity; deduplicate replaced old paths before adjusting live-cell totals.
5. Within one root, reject repeated physical cells, overlapping extents, cycles and conflicting identities/checksums for a segment. Sharing occurs across roots; intra-root DAG deduplication is unsupported. Validate complete cold graphs and preserve unprovable dependencies during reclamation.
6. Add a budgeted paged builder and move-only sealed plan alongside `ImageBuilder`; keep existing constructors and publication calls working. Reuse mutation collection/order/collapse, then replace affected paths directly rather than scanning unchanged records through the flat merge path. Emit final changed nodes postorder into bounded packs.
7. Give reader refresh, coordinator candidate comparison and relay bundle selection one verified semantic-equivalence check for equal complete cursors/profiles. Keep exact descriptors and directory identity for publication/feedback binding. Maintenance refreshes the physical head without clearing valid wire history or creating a new window edge.
8. Dispatch generation ownership and root reclamation by actual layout version. Preserve v1 lease bytes; support v2 root ownership only together with matching recovery and collector logic. Keep unsupported formats rejected and v1 writers active until the complete v2 path qualifies.

## Complete-component acceptance

1. Compare v1/v2 complete key sets, revisions, rollout behavior, repeated mutations, deletion/reinsertion, progress, empty scopes and default fallback. Logical commitments agree across snapshot builds, different update histories and compaction.
2. Inject failures and process kills around dependency writes/syncs, selection rename and directory sync. Recovery yields a valid old or new selection; ambiguous selection fences. Cache visibility and feedback never precede required durability.
3. Exercise shared segments across current and pinned retired roots, cross-process readers, late lease acquisition, saturation, cancellation and facade loss. One-unit-short admission preserves selection; final release conserves physical charges.
4. Reject malformed extents, overflow, cycles, excessive depth, overlapping ranges, invalid counts and checksum/identity conflicts. Qualify explicit migration and unsupported-format rejection.
5. Run paired 100k/500k workloads with one, ten and 1,000 changed keys, progress-only updates and full snapshots. Record CPU, written bytes, syncs, validation counts, owner lateness, reader latency, mappings, descriptors and disk amplification. Single-key warm work follows affected pages and paths; progress-only work follows root metadata.
6. Qualify authority → relay → agent → separate readers, restart/migration, Linux/FreeBSD, ASan/UBSan, assembled TSan, reader/agent/relay build profiles, legacy regression and relevant sibling builds.

### Snapshot construction measurements

The private snapshot builder consumes sealed distribution rows through canonical trie events and packs pages into immutable segments. Its move-only construction output retains no source snapshot or predecessor root. This output is not a durable publication plan; recovery, contextual reuse and exact-base publication remain activation gates.

A single sequential paired run on Intel macOS uses the same synthetic corpus for both builders: 25% integer knobs and 75% boolean knobs with one override. At 100k records, v1 produces 8,200,219 bytes and paged construction produces 23,355,996 bytes across 23 segments. At 500k records, these are 41,000,219 and 116,777,749 bytes across 112 segments. Paged physical totals include the root and segment payloads, excluding ownership records and descriptors.

At 500k records, v1 construction and validation take 347 ms and 96 ms; paged construction and cold graph validation take 2,804 ms and 3,030 ms. Paged charged peak capacity is 175,856,263 bytes, including the retained decoded source; process peak RSS is 218,849,280 bytes. These exploratory timings do not establish a read-speed improvement. Sparse mutation and progress savings remain unmeasured, and the complete-component gates must account for this full-state overhead.

Reproduce with `detent_local_image_bench 100000` and `detent_paged_snapshot_bench 100000`, then repeat at `500000`. Both use `benchmarks/local_corpus.hh`; raw results are in `benchmarks/paged-snapshot-measurements.json`. This measures in-memory construction, validation and lookup, without filesystem IO, mapping counts or fleet traffic.

### Construction-output retirement

`PagedImage` remains move-only worker-side construction output. Before transferring it to an owner executor, consume it into a `PagedImageOwner` admitted by the existing retirement domain. Its final handle release queues cleanup without destroying segment buffers on the caller. The cleanup worker accounts retained backing capacity in 8 KiB steps, then releases at most 16 segment rows per pass. Charges remain retained until actual buffer destruction; an initialized short prefix does not refund its unused allocation.

Retirement admission precedes consumption, so a saturated domain leaves construction output available for explicit worker-side disposal. Copied handles pin the immutable image across domain closure. This construction owner does not establish durable segment ownership, reuse authority or mapping retirement; those remain separate production integration boundaries.

`SegmentMapping` admits page-rounded mapped capacity before worker-side `mmap`, checks the regular file's exact length, owner, link count and write permissions, and saves its device/inode identity. It borrows the input descriptor only during construction; callers close that descriptor after verification. Mappings hold no predecessor root. Final handle release queues `munmap` on the cleanup worker; a failed unmap preserves both ownership and charge for retry. A mapping alone provides no checksum, graph or reuse proof.

The native and ASan/UBSan mapping fixture covers length and permission rejection, hard-link rejection, one-unit-short mapping admission with rollback, descriptor closure, unlink while pinned, copied handles and final charge conservation.

`SegmentRegistry` admits bounded row storage before accepting mappings, enforces sorted unique segment identities and a complete-file physical limit, and exposes stable borrowed inputs after metadata sealing. It holds mapping handles directly, with no root ancestry. Retirement drops at most 16 handles per worker pass. Metadata sealing is distinct from graph validation and durable reuse authority. Native and ASan/UBSan fixtures cover pre-seal access, duplicate or over-limit insertion, immutable sealed rows and mapping lifetime after descriptor closure and unlink.

### Bounded dependency discovery

The private `DependencyDiscovery` worker enumerates segment dependencies from the root reference, rather than scanning the directory. It requests an unresolved segment identity and suspends until the recovery operation supplies its admitted mapping. It verifies that mapping's complete-file checksum before checking pages or following child references. Sorted insertion, hashing, page framing, reference traversal and final registry assembly consume explicit byte and row budgets. Expected page/extent/segment counts and reconstructed path length bound malformed traversal; the complete semantic validator subsequently rejects repeated or overlapping cells and invalid definitions.

The discovery constructor admits its control, bounded stack, dependency rows and output registry before a resolver can perform IO. The sealed output contains mapping handles, not a predecessor root. This proves dependency enumeration and physical framing, not named ownership or durable reuse: actual recovery must verify ownership records, directory identity and named file identities before minting its leased-root capability.

File-backed native and ASan/UBSan qualification covers one-byte steps, multi-segment snapshots, unrelated directory files, corrupted complete-file checksums, empty roots, admission failure, bounded disposal and mapped lookup after descriptor closure and unlink. Shared checked reference navigation is also used by cold semantic validation.

Cold validation now records per-segment reachable page and extent totals into the admitted registry before releasing traversal scratch. Transfer consumes byte/row budgets and binds the exact registry input span and storage identity. Totals and the validated root remain unavailable until transfer completes; metadata sealing alone cannot expose them. This retains bounded per-segment state, without a permanent per-page proof map or predecessor root. The file-backed fixture checks aggregate conservation against the root and verifies both one-byte transfer and index rejection.

### Actual leased recovery

Generation recovery dispatches from the managed ownership record's physical layout. V1 vectors and empty legacy leases retain their behavior. A layout-2 record binds an exactly 320-byte root to its selected descriptor. Recovery verifies dependency ownership, complete checksums and semantic graph validity, then rechecks named payload identities and root/storage bindings before sealing a private `LeasedPagedRoot`. That capability retains the exact descriptor, directory and storage identities, immutable registry, state counts and actual kernel root lease. An in-memory validated fixture cannot mint it.

The leased capability retains no ancestor root or SDK view. SDK views retain it through retirement ownership, so releasing a view cannot synchronously destroy a registry or unmap its segments. Root state counts include booleans, integers, overrides and canonical strings; cold validation enforces configured string and per-definition override limits.

The file-backed recovery fixture covers valid and empty roots, per-switch evaluation, string/override/physical limits, mapping pressure, unsafe or missing payloads, unsupported selected layouts, failed-refresh retention and lookup after path removal. Paged writer activation, warm changed-head recovery integration, semantic maintenance equivalence and shared-segment reclamation remain complete-component gates.

### Contextual reuse navigation

`PagedPlanBuilder::reuse` accepts an actually recovered paged base and performs budgeted navigation through its checked mappings. It mints a move-only `ContextualReuse` for a leaf or requested frontier, binding the complete reference and reconstructed entering prefix to the exact descriptor, directory/storage identity, segment ownership and actual file identity. Navigation performs no filesystem work or hashing. The proof retains the recovered root lease until construction/publication settlement; final registries retain mapping handles separately, never these base-root proofs.

This capability proves unchanged data in its original context. Warm validation must still check disjoint frontiers, candidate namespace/watermark bounds and changed-state counts. A valid context proof cannot authorize arbitrary rebasing or reuse after a decreasing watermark. Native and ASan/UBSan fixtures cover one-byte navigation, branch/leaf/namespace frontiers, absent keys, metadata/context mismatch, pressure, moved-proof guards, multi-segment bases and root-lease pinning after facade release. V1 bases require migration before this path.

### Exact-base progress plans

The private move-only `PagedPlan` binds a forward progress update to an actually recovered base descriptor, directory and storage identity. Construction shares the immutable segment registry, retains the actual base lease and writes and hashes only a 320-byte root under byte/row budgets. It neither traverses knob state nor retains the decoded update after construction. Plan storage and handles retire through admitted worker ownership. The registry keeps its original validated cursor; advancing the root cursor does not mint a new recovery capability.

Native and ASan/UBSan progress/reuse checks pass (2/2 in 0.81 and 1.44 seconds). The file-backed progress fixture covers one-byte construction, stale-base and reused-generation rejection, cancellation, incomplete and moved-plan access, checksum verification, retained kernel leases, lookup ownership after path removal and exact admission-charge conservation. The plan remains private and is not accepted by production `DiskStore`; sparse mutation, warm recovery and durable publication remain open.

### Shared mutation ordering

The private `DeltaIndex` extracts the v1 merge's stable sort and last-transaction duplicate collapse into admitted row storage. It contains mutation references and revisions only; sparse consumers can use it without materializing or scanning the old record set. The v1 image builder uses this same implementation, preserving its independent expected-snapshot comparisons. Byte-limited sorting yields when its grant is exhausted. Direct checks reverse input key order and verify final mutation ordinals and revisions across repeated transactions; native and ASan/UBSan checks pass in 0.32 and 0.45 seconds.

### Lexical mapped traversal

The private protocol-free `PagedCursor` walks only the requested namespace or definition subtree, using a fixed admitted path stack and reconstructed-key scratch. Each step spends explicit byte/row grants; a paid byte grant survives until a later row grant arrives. Leaf text remains borrowed until consumption, and rewind supports the encoder's count/emission passes. Actual recovered leases retain mappings without filesystem work, rehashing or predecessor-ordinal scans. Native and ASan/UBSan fixtures pass in 0.67 and 1.46 seconds, comparing every key across small and 20,000-record multi-segment images, then repeating traversal after recovered handles and files are removed. Profile-1 export integration remains pending.

### Profile-1 export integration

The recovered-generation adapter now selects a paged source with bounded namespace preparation and lexical definition traversal. `SnapshotSource::prepare` defaults to immediate readiness for existing sources; the shared encoder waits for preparation before counting. This additive virtual method requires matching C++ headers and libraries. Definition header decoding is factored from full validation, so the already verified source reads fixed framing and streams individual overrides without rescanning each full body. No sibling API changes are needed.

Exact profile-1 byte comparisons pass natively and under ASan/UBSan, including repeated export after file removal, empty roots/namespaces, namespace-index ordering, UTF-8 switch keys and 20,000 records. Linux and FreeBSD pass the six related source/recovery/cursor/progress/merge checks. The full native and minimal reader-only profiles build. Warm sparse graph validation is implemented privately; recovery integration and durable publication remain open.

### Warm progress recovery

Actual recovery accepts an optional previously recovered capability. Forward roots with identical tree references, logical commitments, physical totals, record/namespace counts and profile can share the sealed registry across nondecreasing global revisions. Admission accounts must match. Cold validation now retains the observed maximum override count alongside aggregate state counts, allowing current stricter limits to be checked without a definition scan. The common named-identity, storage and new-root lease checks still run before recovery seals the new capability.

The SDK reader uses this path for paged progress while retaining its existing v1 ownership behavior. New roots share the registry without retaining predecessor roots. Native, ASan/UBSan, TSan, Linux and FreeBSD checks cover one-root-page admission, stricter limits, ownership changes, actual SDK refresh, predecessor kernel-lease release and complete charge conservation. Changed-tree recovery still runs cold; full structural difference validation remains the next reuse gate.

### Sparse validation bounds

Cold validation now retains a 257-bucket override-count histogram with the sealed registry. Its 1,028 bytes cover the current zero-to-256 override limit and include zero-override integer/boolean definitions. Transfer consumes byte/row budgets and remains unavailable until the exact graph proof is sealed. Sparse construction can subtract removed definitions and add replacements, then derive the actual maximum without scanning unchanged leaves. Progress roots share this immutable metadata with their registry. Native and ASan/UBSan graph/snapshot/progress fixtures pass (3/3 in 1.15 and 1.74 seconds), verifying definition, override and maximum-bound conservation under one-byte transfer.

### Changed-path structural overlay

The private worker-owned `TrieOverlay` accepts checked immutable graph inputs and produces structural recipes, without granting publication authority. It copies touched ancestors and leaf bodies into admitted scratch, borrows untouched references, splits compressed prefixes on insertion and rebases surviving children when deletion collapses a unary branch. Old page removal totals accumulate per segment. Traversal, child copies, branch growth/shifts, body copies and disposal use explicit byte/row grants; a missing-key deletion preserves the tree.

Independent full-snapshot rebuilds verify the canonical logical commitment and key counts after every prefix of mixed update histories. Native and ASan/UBSan fixtures pass in 0.34 and 0.41 seconds, covering branch growth, replacements, prefix splits/collapse, missing deletion, empty-root insertion/removal, maximum-length keys, pressure refusal, partial cancellation and physical charge conservation. Actual-base warm validation and sparse payload assembly are implemented; publication-plan admission and durable IO integration remain required.

The private `LeafBody` encoder now supplies snapshot leaf payloads and the changed-leaf adapter. Scalar headers, override framing and canonical ordering use the existing paged codec; callbacks borrow immutable validated rows until encoding or disposal finishes. Disposal charges the allocated buffer capacity before releasing it, and snapshot cancellation tests use bounded nonzero byte grants with a turn limit.

Changed-leaf collection is implemented privately in `DeltaLeaves`: namespace traversal retains an actual recovered root lease, names are copied into accounted scratch pages, touched watermarks are applied in transaction order, and mutation namespace indices and qualified key prefixes are checked against that recovered scope. `DeltaIndex` preserves the final mutation and its transaction revision for each key. The output visits changed watermarks followed by changed definitions, retaining each encoded body until the consumer finishes and releases it under a bounded cleanup budget. Unchanged definitions are never enumerated. This is construction input, not a replacement for exact-base reuse proofs or durable publication admission.

Private sparse assembly is implemented with a bounded postorder traversal over reachable fresh overlay recipes. A preliminary traversal sizes new segment storage from fresh page bytes; untouched references pass through without visiting their descendants. Per-segment subtraction determines retained dependencies and live page/byte totals, while the existing page and segment codecs compute all new physical and logical commitments. The assembled artifact carries new segment buffers and the complete root descriptor, but deliberately carries no durable reuse authority. The publication plan must retain its actual base and verify reused reference contexts before accepting this construction output for IO.

`PagedPlanBuilder::reuse_at` resolves the exact `(reference, entering prefix)` needed by structural difference validation. It reads bounded headers and child edges from the actual leased base and returns a contextual proof only at the requested entering position. This avoids probing a reused branch for a representative descendant key; reference or context mismatches return no proof.

### Warm structural difference validation

The private, protocol-free `WarmGraphValidator` retains the actual leased base and validates unknown segments and new pages with their physical and logical commitments. A new-segment bitmap rejects overlapping extents or repeated parents; reused pages require an exact contextual proof from the base. The reverse lexical walk collects reused frontiers without entering their descendants. A second walk over the old tree prunes those frontiers and derives removed pages and live bytes independently of construction statistics, without an old-page bitmap or old segment hashing.

Namespace names are copied from the actual base into accounted rows. The scope stays fixed, new watermarks cannot decrease, and new definitions must fit the resulting namespace revisions. Changed and removed leaves update aggregate strings, definition types, override counts and the fixed override histogram. Final page, byte, segment and record totals must match the candidate root. Completed proof transfer verifies the target registry's exact ownership and byte identities before sealing its reachability and histogram; graph validity does not establish durable named dependency ownership.

Native and ASan/UBSan checks compare independent full snapshots and cold validators for repeated mutations, deletion and progress, including a 20,000-definition shared-segment base. They check exact new-segment hash bytes, independent removal totals, mapped registry transfer, malformed root totals and reused references under incorrect prefixes, one-byte grants and cancellation conservation. The SDK profile builds without adding ingestion dependencies to the reader. Move-only sparse plan construction is implemented privately; durable admission and warm recovery integration remain the next boundaries.

The four warm validation/reuse/plan checks pass under ASan/UBSan in 3.31 seconds, TSan in 21.06 seconds, Linux in 1.06 seconds and FreeBSD in 1.50 seconds. The full native and minimal SDK profiles build. These component checks do not establish fleet throughput or enable paged writing.

### Sparse publication plan

`PagedDeltaPlanBuilder` consumes a validated delta and retains the actual recovered base. Construction uses the shared delta index, changed-leaf encoder, sparse overlay, segment assembler and independent warm validator. Budgeted ordering aligns new segment metadata with validation, while per-segment reachability remains indexed by artifact emission order. The sealed plan binds the exact base descriptor, directory and storage identity to its new descriptor, root, buffers and semantic counts. It retains no publication authority for a different base.

Decoded input, construction scratch and output buffers have retirement owners admitted before construction. Dropping or cancelling the builder queues cleanup; quota failure makes further construction terminal. The plan retains the base kernel lease after its initiating recovered facade disappears. Whole-file disk reservations, durable owner records and dependency barriers are still required at `DiskStore::begin` before the plan can publish.

### Validated disk-publication component blueprint

The Architect validates extending `DiskStore` with `begin(PagedPlan&&)` while preserving its v1 publication path. Exact descriptor, directory, storage, cursor, profile and admission checks precede plan consumption. The complete new-artifact table and preparation controls are funded before mutation. A separate retirement owner drains variable child preparation state without cycles or work on its final caller.

Preparation is serial: each segment ownership record seals before its pack, followed by the root lease, root image and descriptor. The existing closed-file preparation supplies write, file sync, directory sync and authentic closure. Only then may descriptor replacement begin. Accepted replacement immediately makes selection uncertain; the final directory barrier establishes known selection and permits accounting settlement. Healthy cancellation finishes the active artifact and accounts for exactly the sealed prefix. Errors preserve conservative debt and fence the store.

For `S` new packs, reserve `320 + 128 + 176 + sum(pack_length + 128)` bytes, `3 + 2*S` entries and one generation. Reused packs already belong to retained census debt; reachable page extents cannot substitute for complete physical pack lengths. Startup retains actual recovered layout and dependency evidence, reconciles each selected dependency serially and completes its durability barriers before publication becomes available.

Collection first reaps unpinned root artifacts using exclusive root leases and held/named identity checks. A fresh non-mutating enumeration then marks dependencies of every surviving v2 root; unknown or corrupt surviving roots block segment sweeping. Namespace mutations remain paused until marking and cursor closure finish. Publication must either enter its complete dependency set into the epoch before creation or invalidate the epoch and restart marking. Selected certificates derive from admitted segment metadata and share unchanged state for progress, avoiding a selected-knob walk after every update.

Unmarked packs require independent ownership, storage, segment, length and file-identity checks. Pack unlink and its barrier precede owner-record unlink and its barrier; authentic closure precedes debt credit. Invalid checksums and unknown partial artifacts confer no deletion authority. Exhausted mark capacity suppresses deletion without truncating protection.

Acceptance covers real snapshot/progress/sparse disk publication and restart, independent snapshot equivalence, faults and process kills at every dependency/selection boundary, cross-process pinned and late readers, orphan and corrupt artifacts, mark saturation, publication between collection batches, exact ledger/census agreement, one-unit-short admission, allocation-free drainage after admission, bounded descriptors and final charge conservation. Native, Linux, FreeBSD, sanitizer, TSan, full v1 regression and sibling compatibility gates precede one consolidated component review. Paged writing remains disabled until these gates pass.

The disk transaction seam is implemented privately. `confirm_recovered` retains the publication plan until actual selected recovery verifies its graph and counts; it then installs independent recovered evidence and releases pending construction ownership. Named reused dependencies pass a serial read-only completion composite before any new artifact creation. Preparation children retain separate publication data; variable child and artifact owners retire in bounded slices.

Root-only reclamation accepts a partial v2 manifest only under verified layout-2 ownership, an exclusive kernel lease and repeated named/held identity checks. It checks every visible fixed byte and every complete dynamic constraint; missing suffix bytes remain unknown. Complete manifests require their full ownership checksum and structural codec. Current admission quotas do not redefine valid format framing. Unsupported prefixes and complete checksum failures preserve debt. After root sweeping, fresh complete reachability marking remains mandatory before any pack deletion.

The publication component passes its consolidated Architect source review after resolving complete-pack recovery admission, optional metadata priority, mixed-layout lease retention and cancellation/error precedence. This acceptance covers the private publication boundary; the operational writer remains disabled.

The shared-segment collector uses a fresh enumeration after root reclamation, with namespace mutation paused through root checks, dependency marking and cursor closure. It verifies explicitly named historical roots rather than following `selected`. Cache hits still require current ownership, manifest and inode verification; cache misses require checked traversal and full graph validation. An incomplete census, unknown root or exhausted mark capacity suppresses segment sweeping. At a settled boundary, publication invalidates the completed mark epoch before admitting new artifacts; collection then restarts marking. Sweeping verifies each unmarked pack independently and finishes pack and ownership barriers plus authentic closure before credit.

### Shared-segment collector component review

The Lead Architect accepts the complete collector source with no remaining P1/P2 findings. A fresh image-and-lease census certifies every surviving root before marks seal. Unknown surviving roots suppress deletion; invalid unmarked candidates retain their debt and allow later candidates to proceed. Complete pack deletion requires both directory barriers and authentic closure. Independently verified owner-only residue requires actual payload absence, owner unlink, a directory barrier and authentic closure. Supported empty legacy anchors classify only exact flat headers and grant no reader or deletion capability. Optional certificates yield on read-only pressure, and a fresh epoch starts only after abandoned work drains. Session and collector controls remain owned through accepted tails after facade closure. Whole-product acceptance and writer activation remain separate gates.

## Validated migration and maintenance component

The Lead Software Architect gives source-review GO to mapped admission and changed-head recovery, with no remaining P1/P2 source blocker. Component acceptance requires the recorded native, sanitizer, platform and footprint gates. Operational v2 writing remains disabled.

The next complete component follows this validated blueprint, with one consolidated review after implementation:

1. Add a builder-free borrowed state cursor over actual flat and paged recoveries. Keep namespace watermarks, definition revisions, rollout presence and ordered typed overrides; retain real leases and bounded key/lookahead scratch.
2. Share complete semantic equality across reader refresh, Coordinator and relay Bundle selection. Equal complete cursors and profiles are prerequisites. Validated paged roots compare canonical logical commitments; cross-format roots compare the complete borrowed stream outside installation locks.
3. Extend private plan intent to forward publication and sealed equal-cursor maintenance. Bind actual optional flat/paged bases, exact selected descriptor, directory/storage identities and semantic equality. Preserve trusted reset as separate authorization.
4. Feed borrowed state into existing postorder trie, leaf, page and segment encoders for migration and replacement snapshots. Traverse full state when required without reconstructing decoded snapshots or override maps.
5. Compact bounded sparse pack sets through structural relocation, including index branches and affected ancestors. Preserve logical commitment and semantic counts. A complete reference-header scan is permitted initially, with measured traversal cost and bounded selected packs, recipes and output.
6. Publish maintenance at the settled delivery boundary, recheck the exact base, recover and confirm the durable physical head, then replace ownership. Preserve wire history, export leases and feedback; equal-cursor maintenance creates no publication edge. Old packs earn credit only through the existing collector.
7. Keep v1 the default and preserve callable APIs. Paged input needs the shared accessor or verified downgrade before legacy delta construction. Older binaries require upgrade before reading v2 storage; no sibling completion API change is required.

Qualification covers complete cross-format state equality, namespace prefixes, empty scopes, NUL-containing override keys, watermark changes and deletion/reinsertion; migration and maintenance faults/cancellation at every durability boundary; branch-only, leaf-only and mixed-pack compaction; pressure refusal before selection; pinned readers and exact debt; assembled authority/relay/agent/readers with concurrent maintenance; reproducible 100k/500k construction, recovery, compaction and reclamation measurements; native, Linux, FreeBSD, sanitizer and minimal SDK profiles. V2 configuration activation follows consolidated component acceptance.

## Validated snapshot and delivery integration

The Architect's snapshot/delivery blueprint retains the existing completion transaction and authentic recovery settlement. Fresh publication captures the real managed directory and storage identity with no recovered base; all replacements retain the actual selected descriptor and generation.

1. Independently validate newly encoded snapshot packs and capture counts, reachability and histogram before sealing fresh, forward or reset construction intent. Same-cursor snapshots return bound equality evidence and acknowledge the existing physical selection.
2. Authorize identity reset only through `DiskStore::begin_reset(plan, TrustedIdentityReset)`. A decoded cursor and a reset-tagged construction result supply no permission. Check current selection and configured target identity/profile before admission or transfer.
3. Share target-aware warm eligibility between mapping admission and actual recovery. Fresh and reset targets recover cold; retain the prior generation through candidate recovery and account for physical coexistence.
4. Run candidate construction on the existing configuration worker, settle its authentic completion on the owner, and publish at the settled boundary. Recover and confirm actual selection before replacing owner/sink state or issuing upstream feedback.
5. Add opt-in paged writer configuration with v1 as the default. Migrate a configured flat head before accepting paged deltas/progress; obtain a trusted reset snapshot instead when the head belongs to the configured previous identity.
6. Extend the shared borrowed source access for v1 construction over a paged predecessor. Traverse sequentially with budgeted preparation and bounded reconstructed keys/overrides, retaining the existing merge-reference index and avoiding a decoded full snapshot.
7. Qualify fresh/forward/reset binding, lower-revision authorized reset, unchanged/conflicting snapshots, worker cancellation and assembled paged authority/relay/agent flows. Review the complete delivery integration before activation.
