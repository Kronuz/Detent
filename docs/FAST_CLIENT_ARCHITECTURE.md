# Fast attachment to daemon-prepared local data

**Date:** 2026-10-25. **Status:** Validated architectural blueprint; implementation and qualification pending.

The final application client must attach to prepared local data and return its first verified knob in **under 1 ms on the measured resident-data host**. Measure from entry into `Client` construction through the returned value, including trust checks, leases, mappings, hashing, allocation and runtime initialization. Process launch and dynamic loading are separate measurements. Cold storage, page faults and arbitrary descheduling cannot provide an unconditional deadline.

## Problem and chosen mechanism

A fresh reader currently calls full generation recovery. The preserved 5M corpus takes 47.8–58.8 seconds to open and validate, separately from exhaustive oracle checks. Point reads afterward are local and substantially shorter. Repeating full graph validation in every application is a release performance blocker.

Use the existing selected descriptor, independent generation lease and 320-byte paged root. Authenticate their filesystem provenance and validate only pages visited by a lookup. Keep the paged-v2 data representation and shared evaluator. Do not require a live-daemon endpoint, eager segment-directory transfer, or mapping all 1,441 packs before the first value.

```text
Trusted publisher files → selected descriptor + independently leased root
  → verified root commitment → checked lookup path → borrowed definition
  → retained mappings and verification cache for subsequent reads
```

The references already contain physical and logical hashes. `paged::verify_page()` verifies both. File ownership authenticates the configured publisher under the protected-directory contract; hashes detect corruption along the accessed path. A persisted boolean saying that data was validated is not that proof.

## Publication and provenance

1. Resolve from an authenticated directory anchor or walk and pin trusted ancestors. Check the expected publisher UID, non-writable ancestry and applicable ACL policy. The current final-component `O_NOFOLLOW` and matching directory/file owner checks do not establish that complete contract. Reuse the existing descriptor-based trust predicates and lease machinery rather than add another policy engine.
2. Prove that sealed publication plans establish global semantics before selected replacement. Full recovery after selection cannot retroactively authorize an earlier fast read. Audit fresh snapshots, exact-base deltas, progress and equal-cursor maintenance separately. Keep untrusted standalone recovery available.
3. Bind root bytes to the descriptor checksum, generation ownership, storage identity, cursor, scope, profile and configured limits. Retain exact aggregate statistics through a bounded descriptor-bound publisher metadata record if needed. The existing root does not contain boolean/integer totals, aggregate string bytes or maximum override counts; the client must not invent them or scan the dataset merely to populate health statistics. The metadata encoding and durable publication belong to this component's implementation.
4. Trust the publisher to preserve published inode bytes. A read-only mapping cannot protect against another writable handle. Root and hostile same-owner writers are outside this boundary, as they are for a live authenticated daemon handoff. Corruption of an unused page is detected when accessed.

## Checked access and ownership

Use one shared page-access mechanism beneath the existing evaluator. Authenticate each visited reference, check overflow-safe extents, framing, ordered labels, complete reconstructed keys and definition bodies before access. Verify a branch before returning a negative lookup. Namespace-dependent validity requires the corresponding watermark path or equivalent publisher-bound evidence. Corruption produces a source error, not a missing-key result or silent default.

Resolve only required packs, using canonical names relative to the retained directory. Keep opened mappings alive through their borrowing views. Admission bounds mapping bytes, descriptors, cache metadata and old/new coexistence. Cache successful page verification only against the exact immutable mapping and complete reference; a bounded cache may evict verification entries without invalidating borrowed bytes. Cache-hit lookups must not allocate or perform filesystem/network operations.

Acquire and recheck an independent shared generation lease before opening dependencies. A surviving root must protect every reachable pack, including one not yet opened by that reader. Establish this across collection, compaction, restart and late access. Never substitute a newer pack or root after an old view encounters an error. Retirement remains on the existing cleanup machinery.

## Client lifetime and refresh

A long-lived client retains its installed root and mappings. Share attachment state within a process for the same configured source; defaults and CLI overrides remain per client. Include the first context's initialization in first-read measurements. Defer optional work if measurements show it consumes the deadline.

Refresh checks only selection metadata before attaching a changed root. Keep the existing bounded polling mechanism initially; notifications are an optional measured improvement. Install coherently through the existing atomic view handoff, enforce monotonic revisions and authority identity, and verify logical equivalence for same-cursor physical replacement. Pinned views retain their old generation. Already attached readers continue offline; rooted-file attachment also permits a new offline reader when publisher provenance remains trustworthy. Fresh offline readers need an external revision floor to detect restoration of an older otherwise valid directory.

## Implementation and qualification

1. Audit pre-selection semantic proofs and collection protection. Implement the shared provenance, independently leased root and checked page-access seam with corrupt-reference/body, missing-key and bounded-admission fixtures.
2. Integrate bounded publisher metadata and trusted prepared-reader attachment without changing standalone full-recovery behavior. Complete publication, cancellation, restart and old-state compatibility checks.
3. Integrate final-client initialization, shared source lifetime and incremental refresh. Preserve defaults precedence and explicit source failures. Measure concurrent readers and held old views.
4. Run one complete code/security review and composed qualification. Measure 100K, 500K and 5M with representative first keys, deep paths, absent keys and maximal branches. Record first-read distributions and maxima, CPU, page faults, opened/mapped packs, allocations and refresh work. Include cache pressure and cold storage separately.

Require zero full-graph traversal or full-segment hashing in ordinary trusted attachment. Report warmed point reads independently, targeting sub-microsecond hot reads without promising nanosecond scattered access. If path-dependent opens and mappings fail the measured first-read target, evaluate bounded append-only arena backing as a canonical storage change. A redundant full-snapshot mirror is not the default solution.

The isolated implementation includes opt-in synchronous Client attachment, metadata publication and lease-protected metadata reclamation. The SDK-only profile passes 83 checks before publication integration. Focused publication, cancellation and reclamation checks pass; the full updated product suite and independent review closure remain pending. Caller corpus bounds are enforced; `ViewLimits::prepared()` supplies format-wide logical bounds while retaining bounded heap and shared process mapping admission. Failed refreshes preserve the installed view, and a pinned old view remains usable when a replacement exhausts their shared mapping budget.

The [exploratory 5M first-read measurement](../benchmarks/prepared-client-5m-20261025.json) includes source setup, runtime initialization, Client construction and the first checked live integer. Forty fresh processes querying distinct corpus-derived keys record 1.038 ms median and 1.303 ms maximum, with zero major faults; 14 samples are below 1 ms. Attachment contributes 0.492 ms median and the first lookup 0.526 ms median. Process launch and dynamic loading are excluded. These samples do not meet the sub-millisecond acceptance target or establish a p99 bound. Earlier one-key samples have narrower scope and do not replace the spread-key result.

Default activation, old-state preparation, trusted standard path aliases, shared attachment contexts, unopened-pack collection, corruption and platform qualification, and representative first-read distributions remain open. The trust/publication proof and deferred pack protection remain acceptance gates.
