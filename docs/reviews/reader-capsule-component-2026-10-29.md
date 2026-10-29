# Detent complete capsule component review

Date: 2026-10-29. Reviewed frozen **Detent:fe0faf0**, including core Detent:01aa8b0 and reader Detent:9465d8a, against Detent:12bad92. Scope: the complete reader, format, publication/accounting, collection, configuration, readiness and rollback-verifier change. Checked the [approved architecture](reader-capsule-20261029/source/docs/READER_CAPSULE_ARCHITECTURE.md), [original blueprint](reader-capsule-blueprint-20261029.md), [ledger](reader-capsule-20261029/source/docs/status/reader-capsule.md), calibration notes, relevant unchanged ownership/retirement code, and changed tests. This is the component review, not a per-commit approval.

## Verdict

**APPROVE WITH CONDITIONS.** No blocking implementation defect found in the reviewed source. The implementation follows the owner's approved refusal and coordinated-rollout contract. Complete the qualification conditions below before landing/adoption; the prototype screen and green ancestor/prototype CI do not close them. The writer remains Legacy by default. Resource adoption and CL-2 remain independent gates.

## Blocking findings

None identified by this bounded source-and-evidence review. No independent fault reproduction was needed or run. The remaining items are uncompleted qualification conditions, not asserted code defects.

## Security & Scale Review

1. **Decoder and prefix bounds.** `src/detent_local/reader_capsule.cc:38-48` checks exact extent before creating fixed subspans, verifies framing and the outer digest, then delegates to the existing storage/root/metadata decoders. `:12-18` binds storage, root checksum, counts and physical extent. The prefix validator at `:51-92` bounds both inputs before slicing, compares the available original-root prefix, binds a complete nested root to the lease checksum, and checks available statistics before full decode. `disk_store.cc:1241-1253` dispatches at the first distinguishing magic byte; one- and two-byte prefixes share the legitimate `DT` prefix. Unknown framing, oversize records, and inconsistent available fields are refused. Prefix validity only supplements existing ownership and exclusive-lease proof; it grants no independent deletion authority.

2. **SDK trust and refusal precedence.** `prepared_reader.cc:117-150` retains selected-descriptor validation and the actual shared lease, including held/named inode equality after locking. `:152-192` captures open errno before subsequent calls and validates originals before reporting missing legacy metadata. A malformed capsule never produces a successful legacy fallback. `:174-203` binds the nested root and metadata back to the selected descriptor and lease and enforces configured admission limits. Trusted ancestry, UID/mode/ACL checks on opened files, and lazy segment verification remain intact. The deliberate absence of standalone-original inspection for a valid capsule is the approved policy, including the possibility that full daemon recovery refuses what an SDK can read. No IPC trust mechanism is added.

3. **Publication, accounting and collection.** `paged_publication_artifacts.cc:64-83` keeps lease → image → metadata → descriptor ordering; the chosen metadata buffer length feeds the existing artifact usage and prefix accounting. `include/detent_local/disk_ledger.hh:54` charges the same additional 432 bytes. The existing `PublicationOperation` still prepares artifacts before selected replacement, marks accepted replacement uncertain, and settles only after the directory barrier. Collection changes the metadata maximum and validator, while `disk_store.cc:887-1025` retains exclusive lease acquisition, bounded `RecordReader` validation, final identity rechecks, payload deletion/barrier, then lease deletion/barrier. Malformed metadata preserves the generation. No new publisher, collector or accounting engine appears.

## Edge-Case Analysis

1. The decoder test checks all 624 truncations, single-byte changes, resealed corruption and cross-bindings. These are useful deterministic cases; they do not replace the pending decoder/prefix fuzz gate. The partial-reclaim fixture exercises 625 lengths, retained leases, corrupt leftovers, actual disappearance and exact census accounting.
2. The two fixture corrections are justified. `test_managed_config.cc:185` resets the deliberately invalid compaction interval before exercising metadata configuration. `test_reader_capsule_partial_reclaim.cc:29-37` still rethrows every store error and accepts only the exact `unsafe named paged root` collection diagnostic. The existing legacy driver's omission of collection-error checks at `test_paged_root_partial_reclaim.cc:13-27`, and the producing check at `paged_named_root.cc:298`, explain the attribution. The new fixture is stricter than that oracle; its artifact/census assertions remain decisive. The [rerun log](reader-capsule-20261029/final-fixtures.log) records both corrected tests and TLS setup passing, 17.55 seconds total. This does not rewrite the original 20/22 result as an all-green first attempt.
3. The lifecycle fixture exercises equal-cursor Republish in both directions, unchanged root/checksum and reused segments, restart, retained views, corrupt capsules blocking collection/verification, and eventual successful legacy verification. `GenerationReader::refresh` retains its directory, revision and same-cursor logical/profile checks. `PreparedFiles` remains owned through lazy access and view retirement, so metadata buffer destruction does not release the generation lease.

## Dependency & State Audit

`metadata_ready` compares capsule contents against fully recovered root/count evidence before checking the requested encoding. The coordinator captures that encoding for its worker and uses its existing maintenance publication when readiness differs. Configuration rejects Capsule with a non-paged writer; default construction remains Legacy.

`reader_metadata_compatibility.cc:39-85` binds a newly traversed directory to the held session, checks every `.read` with bounded record reads and held/named identity verification, rejects noncanonical names and unsupported/corrupt contents, and rereads selected at the end. The CLI acquires the existing managed owner session before this scan. Session/backend retirement retains the owner lock through completion. This excludes cooperating publishers using that same lock; it does **not** establish the separate CL-2 mixed-locking-protocol guarantee. No shared pin changes are present.

## Scalability Check

SDK metadata remains fixed-size: 928 bytes across three files per successful capsule attachment, with the existing three-attempt cap. `RecordReader` grows by 432 bytes per instance, including instances used with Legacy. Collection preserves its scan/work allowances and bounded batches. The offline verifier uses constant per-record memory but walks the whole directory without an explicit entry/deadline cap while holding the owner session; its duration is proportional to retained entries and filesystem latency. It is not an SDK hot-path operation.

The published screen measures the reader prototype, not the complete frozen publication implementation: paired wall savings are 44.764/27.195 µs in private/canonical arms, with four fewer allocations, private +4 minor faults, and measured probe text/local-stack growth. Those costs remain part of qualification; no broader footprint or acceptance claim follows.

## Conditions before landing/adoption

1. **C1 — Complete frozen-source qualification.** Attach complete-component CI and supported-platform sanitizer results for the integrated source, including privileged ownership/ACL/race cases. Ancestor and reader-only CI are insufficient. Retain the reported 1,194 publication fault/cancellation cases across both encodings and the lifecycle evidence with exact source/build attribution.
2. **C2 — Decoder safety.** Run the required bounded full/prefix decoder fuzz qualification, including unknown versions, resealed nested inputs, truncations and both original-root-prefix states. Preserve failures and corpus artifacts.
3. **C3 — Canonical closure and rollback exclusion.** Exercise publisher/verifier mutual exclusion and rollback on the canonical resource closure. Preserve CL-2 as its own release requirement; do not infer `Both` locking from the current completion pin or this review.
4. **C4 — Unchanged performance acceptance.** Require 200/200 uninstrumented resident-data first reads below 1 ms in **each** resource arm on the unchanged real 5M corpus and qualified integrated source. Do not substitute the twenty-key screen or pool arms.

## Not run and next stage

No builds, tests, fuzzing, benchmarks or independent probes ran during this review. The corrected-fixture log was inspected directly; the longer native fault/lifecycle outcomes are supplied builder evidence, not independently rerun here. Complete-component platform CI, privileged cases, fuzzing and 200-read acceptance remain open.

Proceed to those qualification gates, without another architecture round unless the mechanism or approved contract changes. After they pass, I recommend **Capsule as the first release's managed-agent default**, with explicit Legacy mode for mixed-SDK rollout and rollback. Make that a separate owner-approved deployment/default change: existing pre-release installations must pin Legacy before a mixed-version upgrade. This review authorizes neither a default flip now nor silent migration of those installations.
