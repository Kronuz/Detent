# Validated Architectural Blueprint: disk lifecycle

**Status:** Lead Software Architect design GO (frontier model, max effort), October 7, 2026. This read-only supplement plans the remaining Stage 4 resource component. It does not accept unimplemented storage lifecycle behavior or complete Stage 4. Implement the whole component autonomously, qualify it, then request one consolidated source review.

Use a streaming census, a bounded active-work registry, and completion-driven reclamation anchored by durable ownership records. Preserve generic journal formats and interfaces. Keep heap capacity, mapped virtual bytes, logical disk commitments and native/runtime resources separate.

## Ownership records and reader compatibility

Keep `DTIMAGE1`, the 176-byte `DTSELECT` descriptor and generation filenames unchanged. Add Detent-only immutable `storage.meta` and a versioned record in each managed generation's existing `.lease` file. Encode integers explicitly in big-endian order.

| Record | Offset | Field |
| --- | ---: | --- |
| `storage.meta`, 64 bytes | 0 | Eight-byte magic `DTSTORE1` |
| | 8 | `u32` version 1 |
| | 12 | `u32` record length 64 |
| | 16 | Nonzero 16-byte storage identity |
| | 32 | SHA-256 of bytes `[0, 32)` |
| Managed `.lease`, 128 bytes | 0 | Eight-byte magic `DTLEASE1` |
| | 8 | `u32` version 1 |
| | 12 | `u32` record length 128 |
| | 16 | Storage identity |
| | 32 | Nonzero 16-byte generation identity |
| | 48 | `u64` intended image length |
| | 56 | `u32` image layout version 1 |
| | 60 | `u32` descriptor layout version 1 |
| | 64 | SHA-256 of the intended sealed image |
| | 96 | SHA-256 of lease bytes `[0, 96)` |

Validate exact lengths, magic, versions, nonzero identities, checksums and supported layout bounds before treating a record as proof. Ownership validation does not reject an otherwise supported old generation merely because it exceeds today's smaller admission limit. Storage identity is independent of scope, Context and restore epoch. Checksums detect corruption; the trusted directory and cooperating writer protocol establish authority.

Upgrade SDK readers first: existing binaries require empty leases and cannot read managed records. New readers accept both empty legacy leases and validated managed leases, then managed publication can be enabled. A managed reader validates `storage.meta` and binds lease generation, intended image length and checksum to `selected`. Empty legacy leases remain readable and confer no reclamation authority.

Place the pure record codecs beside local layout code. The SDK reader gains no completion, journal or networking dependency.

## Shared session and identity bootstrap

Factor `StorageSession` from publisher ownership. Retain backend, stable owner lock, directory identity, allocation context and a fresh per-session completion identity. Publication, census and GC share this session; they do not reacquire its lock. Tokens combine the session incarnation with monotonic operation and step numbers; persistent storage identity is not an incarnation token.

Only the documented fresh, empty-directory profile bootstraps a new identity. Admit the owner-lock entry before creation and admit 64 bytes plus one entry before creating `storage.meta`. Write the complete record, synchronize the file, then synchronize the directory before any generation Create. This barrier also establishes the new owner-lock name.

An existing legacy store may gain `storage.meta` after successful recovery and complete quiescent census, provided no evidence indicates a previous managed identity. Existing artifacts retain their charges and proof-based protection. Never replace an existing identity or infer one from an arbitrary lease record.

A complete valid identity with uncertain original publication may be validated and synchronized during recovery. A partial, corrupt, unsupported or conflicting identity fences managed mutation and GC. Preserve it and report the condition rather than generating a replacement identity.

## Logical ledger and admission

Count logical named bytes, top-level entries and canonical generation lease anchors; report outstanding reservations separately. Count every inspectable top-level entry, including preserved foreign files. Count malformed canonical lease anchors conservatively. Unanchored image/descriptor artifacts remain charged as bytes and entries and are reported separately.

Every managed family creates its anchor first and removes it last. Before the first family Create, reserve the full peak for image length `L`: `L + 128 + 176` logical bytes, three entries and one generation. Replacement still reserves both descriptors before Rename. Admit actual heap controls and the active registry slot separately; disk reservations are not heap reservations.

Check each dimension without unsigned subtraction underflow. Recovered usage may exceed newly configured limits: retain the complete debt, reject additions and permit safe reclamation. Never clip inventory to a quota ceiling.

Logical admission reserves no physical filesystem blocks. Sparse files, allocation units, filesystem metadata, unlinked open handles and other filesystem users remain outside that claim. Allocated-block counts may be diagnostics, not a replacement quota currency.

## Bounded streaming census

Census holds the owner session with publisher/GC mutations paused and preceding originals settled. Retain one cursor, a bounded name buffer, fixed metadata/record scratch, checked counters and a bounded diagnostic sample. Keep no directory-sized vector, sorted name set or generation graph.

Advance `NextInto → InspectEntry`, with bounded record reads where needed. A missing entry during a supposedly quiescent pass, unsupported inspection, arithmetic overflow, scan-work exhaustion or genuine IO failure makes census incomplete. Incomplete counts cannot initialize admission. The census-work allowance is independent of quota limits, so an over-quota store can still obtain exact counts within its work allowance.

Unexpected directories fence this flat-store profile; preserve them without recursive traversal or treating their `st_size` as their contents. Preserve symlinks, special files, foreign ownership and multiply linked entries without following their targets. Account inspectable named contributions and mark unsupported cases. Quarantine means preserved, excluded from automatic deletion and reported; it does not rename or move entries.

## Protection and lifecycle

Use a fixed-capacity active registry, normally one publication and one bounded GC batch. Each slot records generation, incarnation, phase, reservation/debt, original count and protection count. Allocate it before use. Slots do not own operations; operations retain the session and slot capability.

| State | Protection and charge |
| --- | --- |
| Reserved | Full peak held; cancellation before any submission can refund it |
| Staged | Active protection; durable lease proof precedes image creation |
| Prepared | Protected until selection or explicit abandonment |
| Selection pending | Protect both possible generations |
| Selected | Durable descriptor protects its generation |
| Retired or abandoned | Eligible only after originals/protection end and exclusive lease acquisition |
| Uncertain | Preserve roots and debt; fence mutation pending recovery |
| Reclaiming | Retain exclusive leases and deletion debt through originals and barriers |

After restart, unselected owned families need no historical distinction between retired and abandoned: both use the same reclamation rule. Reader-held generations require no persistent catalog entry; shared kernel leases protect them. Image-open originals acquire and retain their shared lease before image opening and through authentic settlement.

## Retained publication settlement

Disk settlement belongs to retained operation/store state, not solely `ImagePublisher::advance()`. The facade may disappear or report cancellation after authentic Rename and DirectorySync. Durable selected state and acknowledged client cursor are distinct.

```text
Reserve family and active slot
  → Create/write/sync lease → DirectorySync
  → Prepare image → Prepare descriptor
  → Replace as selected → DirectorySync
  → Settle durable selection and disk ledger
```

Successful Rename plus its directory barrier protects the new selected generation and settles its charge once, even when cancelled. Acknowledgement can remain unchanged. Credit the previous selected descriptor's 176 bytes and one entry only at that barrier; first publication has no previous descriptor credit. Previous image/lease charges remain until GC.

Further publication compares against durable selected state. If cancellation leaves the client behind, require reconciliation or an explicit controller transition before accepting another base-dependent update. Selection uncertainty protects both roots and fences mutation; malformed or unverifiable recovered selection disables GC.

## Abandonment and uncertain effects

Release an unsubmitted reservation only when no Create was accepted. Accepted work retains reservation and active protection through authentic completion. Healthy cancellation before selection may hand the settled family to GC. Keep debt until cleanup succeeds or a quiescent reconciliation establishes actual remaining footprint. Release unstarted future-file capacity only when retained state proves no original can create it.

Failed Create can leave an entry after permission-setting or another later failure. Its exception does not prove absence, and `EEXIST` does not authorize deleting a collision. Preserve uncertain effects and fence/reconcile.

A valid durable managed lease proves ownership of derived image and temporary descriptor names, including interrupted payloads within declared bounds. Validate the whole family before its first unlink. Malformed/foreign proof, conflicting identity, unsafe metadata or oversized/conflicting members preserve the family. Empty legacy leases and unprovable partial anchors stay counted and preserved, even when they cause admission pressure.

## Completion-driven reclamation

Keep one forward-moving cursor across bounded turns and consider canonical lease anchors. Check selected state and active protection, safely open a fresh lease, acquire `LOCK_EX | LOCK_NB`, then revalidate its proof and named inode. Skip busy leases without waiting or spinning. Do not upgrade a reader's shared handle: lock conversion can release its prior lock. This remains an advisory protocol for cooperating processes. [FreeBSD flock contract](https://man.freebsd.org/cgi/man.cgi?query=flock&sektion=2&format=html)

A bounded batch, initially at most eight families, may share barriers while retaining every exclusive lease:

```text
Hold leases; validate complete families
  → Remove images and temporary descriptors
  → DirectorySync
  → Remove lease names
  → DirectorySync
  → Close leases; credit actual deletion once
```

Keep `selected`, `storage.meta`, `owner.lock` and unknown suffixes outside candidate deletion. Recheck protection before beginning deletion. Credit observed identities/lengths rather than declared maximums, and never credit idempotent absence twice.

The initial implementation refunds only after the final batch barrier and handle settlement. Midway failure retains unreconciled debt and fences mutation. Shutdown waits for originals before releasing native handles and cannot report successful cleanup or refund without completed barriers. Readers opening an old lease before unlink recheck its link state/identity before image opening and retry on mismatch.

## Reusable completion primitives

Add caller-buffer scanning, no-follow entry inspection with optional absence and stable identity, held-file inspection, nonblocking shared/exclusive lease operations, and worker-side handle close/release. Metadata validation and lease composites use the existing bounded fallback executor, never raw native deletion that bypasses validation and never whole-Worker offload.

Preserve legacy `next()`, `Completion::name`, unique-return APIs, primitive ordinals and public legacy statistics arrays. New virtual methods default to unsupported; establish managed-profile capability before activation. Expose new counters separately. Retain operation-owned scratch and alias handles through originals, without an operation aliasing itself into its own request.

## Modules, scheduling and qualification

Split local lifecycle into `DiskLimits`/`DiskStats`, `StorageSession`, `DiskLedger`, `InventoryOperation`, managed generation/publication work and `ReclaimOperation`, coordinated by `DiskStore`. Independent admitted state owns unique cursor/file controls; requests alias that state, and state never owns its operation or requests. Admit actual collector controls, scratch and registry capacity before publication admission so cleanup requires no invented allowance.

Startup performs one streaming pass; GC performs bounded work per anchor and never restarts a whole-directory scan per candidate. Metadata scales with configured active slots, batch size and diagnostic sample, not historical generations. Ownership-based GC does not reread payload bodies. Selected recovery retains full integrity and semantic validation. Mutating directory cursors can omit/repeat entries, so GC passes are opportunistic and revalidate candidates; enumeration never resets a live ledger.

Measure metadata calls, barriers, CPU, admitted capacity and reclaimed logical bytes over growing directories before asserting a scaling win. Report reader-blocked attempts as events rather than an unmeasured distinct pinned-generation count.

1. Validate independent 64/128-byte vectors, every corrupt field, dual-reader compatibility, identity substitution, bootstrap faults and unsupported profiles.
2. Test large/quiescent inventory, over-quota startup, overflow, missing entries, scan exhaustion, foreign files, directories, symlinks, hard links and special files; demonstrate bounded actual allocation and linear metadata work.
3. Exercise each quota dimension one unit short, first selection versus replacement, full pre-Rename peak, held-reader pressure and preservation of the current root.
4. Inject before/after failures for Create/permissions, short writes, file barriers, Rename, deletion and both directory barriers; retain originals across cancellation, facade destruction and close.
5. Prove successful cancelled publication settles durable selection/descriptor credit without client acknowledgement. Pause GC at locking, each unlink and each barrier; verify exact-once credit, partial batches, reader-open races and process-death lease release.
6. Kill/reopen real processes around identity bootstrap, lease sealing, selection and both GC barriers; compare recovered accounting with independent census and preserve unsupported/unprovable artifacts.
7. Run standalone completion, journal compatibility, full Detent regression, native macOS/Linux/FreeBSD, ASan/UBSan and focused concurrency gates. Record final revisions and environmental limits, then review the whole finished resource/storage component.
