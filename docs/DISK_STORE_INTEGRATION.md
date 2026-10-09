# Validated Architectural Blueprint: integrated DiskStore lifecycle

**Status:** Lead Software Architect design and corrected-source GO (frontier model, max effort), October 8, 2026. Component accepted after corrected-source review, native and ASan/UBSan regression, focused fully instrumented TSan, and three-platform scale qualification. This amendment supplements the [disk lifecycle architecture](DISK_LIFECYCLE_ARCHITECTURE.md); component acceptance does not activate production publication or complete Stage 4.

## Coordinator and ownership

Add `disk_store.hh/.cc`, keeping supporting state machines beside it initially. Reuse the qualified session, collector, inventory, bounded reader, ownership codecs and generic preparations. One worker-owned coordinator owns retained startup, publication and reclamation operations.

```text
DiskStore → StoreCore: ledger, roots, protections, session, controls
         → StartupOperation → StartupData → StoreCore
         → PublicationOperation → PublicationData → StoreCore
                 → Three preparations → PublicationData
         → ReclaimOperation → ReclaimData → StoreCore
```

StoreCore, payload data and protection entries own no operations. The publication operation owns its three preparations; their byte/admission owners may retain independent PublicationData, which owns no children. Accepted drivers retain the entire outer operation through terminal cleanup, including intervals between primitives. Closed controls, disk permits and reader composites do not independently authorize deletion or settlement.

After observing a completed, non-in-flight publication, `advance()` preserves terminal status/error as scalar metadata and releases the coordinator's attempt graph. An idle store no longer retains the complete published heap image while the next candidate is received or built. External operation handles still retain their graph and physical charges until their final release. `publication_ready()` exposes the existing availability predicate, including a paused collection boundary, so an owner scheduler can start a waiting sealed candidate before resuming collection.

The focused retained-original regression fails before this correction because the image remains owned after its last external operation handle is released. It passes after correction on macOS, Linux and FreeBSD and under ASan/UBSan and fully instrumented TSan. The full DiskStore fixture also passes natively (290.80 seconds), under ASan/UBSan (332.01 seconds) and TSan (321.58 seconds), on Linux (238.92 seconds) and FreeBSD (9.78 seconds). These shared-host runs include substantial fixture creation and cleanup; they do not measure publication throughput or an execution-time improvement.

Use explicit `StoreOpenMode::{FreshEmpty, ExistingManaged, UpgradeLegacy}` and `StoreStatus::{Starting, Ready, Fenced, Closing, Closed}`. Options carry disk limits, census work allowance, default GC slice of 256 inspected entries, artifact mode (default `0600`) and recovery limits. A startup-worker factory takes retirement domain, directory, caller admission and options, returning a Starting store. Expose status, logical stats, actual selection snapshot, runnable operation, advancement, coalesced collection request, close and managed-work drainage. Selection distinguishes known absence, a known descriptor and uncertainty.

Startup begins with uncertain selection. Only the authentic final recovery directory barrier or sealed metadata preparation establishes Known or Absent. Failed or interrupted recovery preserves uncertainty, even when a selected filename exists. Cancellation and close suppress publisher acknowledgement while retained work can still establish and settle a durable selection.

Add a managed `ImagePublisher(std::shared_ptr<DiskStore>)` constructor while retaining the existing constructor as the legacy profile. Its cursor reports acknowledgement separately from actual durable selection.

## Admission and startup

1. Finish additive `CreateInto` and context preparation overloads accepting fresh closed controls. Preserve the separate `reusable_create` capability and the seven-capability collector `complete()` predicate. Valid closed destinations remain closed on failed creation, but possible namespace effects retain debt. Existing Create and journal interfaces remain unchanged.
2. Extend the owning-session factory with configurable artifact mode while preserving existing call forms. Use explicit `0640` for configured group-readable SDK deployments and corresponding directory traversal permissions. Keep the owner lock private.
3. Admit StoreCore, reusable startup/census/reclamation operations and independent states, eight lease controls, scratch file, cursor, fixed buffers, diagnostics, protections and shutdown bookkeeping before publication activation. Their actual allocations use the common account. Collector passes never allocate another operation or state.
4. Give metadata creation a preallocated file control before Create. Admit each publication's complete attempt independently before any family effects. Preassign cleanup identities and bound step space; accepted cleanup never needs a new session identity after revocation.

FreshEmpty requires an exclusively provisioned empty directory. Before calling the owning factory with owner creation enabled, retain a fixed bootstrap commitment for **64 bytes, two entries, zero generations**, covering owner lock and metadata. Expose it while Starting. Do not initialize the ordinary ledger from invented zero census counts. After acquisition, exact census must show only the new zero-length owner lock. Initialize observed usage from that census and transfer the remaining `{64, 1, 0}` metadata commitment into ordinary admission without counting the owner twice. Unexpected contents fail the fresh profile and remain preserved.

ExistingManaged and UpgradeLegacy open an existing stable owner lock and never recreate a missing lock implicitly. When selected exists, perform full generation recovery and semantic validation on the startup/validation worker while holding the session. Verify recovered directory identity against the session, retain only fixed verified descriptor evidence, and release the temporary recovered generation and its ordinary directory owner on that worker. Event-loop completion callbacks do not perform full image validation.

Startup composes inventory with bounded evidence reads. Before forwarding a successful inventory InspectEntry result, copy its bounded name/metadata, forward completion, then park its prepared next request while reading any required record. Close scratch resources before resuming enumeration. Inventory retains its collector claim throughout; do not submit the next name while its observation is still in use or let inventory release its claim with scratch handles open. This gives one forward census with constant additional work per relevant entry.

| Metadata/evidence | Startup decision |
| --- | --- |
| Valid metadata | Retain its identity; verify selected evidence and complete census before activation. |
| Present malformed, partial, unsafe or unsupported metadata | Fence and preserve it; never replace its identity. |
| Missing metadata in ExistingManaged | Reject managed activation. |
| Fresh directory containing only its new owner lock | Create and durably seal a new identity. |
| Missing metadata in UpgradeLegacy | Require complete census and trustworthy legacy evidence before identity creation. |

Upgrade requires every canonical lease to be safe and empty. Any nonempty or uninspectable canonical anchor blocks automatic identity creation, regardless of whether it decodes. Selected must pass full legacy recovery. An owner-lock-only directory can resume interrupted empty bootstrap; otherwise require positive evidence from a validated legacy selected root or safe empty anchors. Preserve ordinary foreign files and unanchored payloads with their charges. They grant no deletion authority.

Retain complete over-quota census debt. If metadata addition exceeds a quota, report pressure before creating it. Under valid managed identity, bad unselected families can be individually quarantined; incomplete accounting or bad selected recovery fences the store. Startup validates root records with RecordReader, compares selected with worker-verified evidence, validates its lease binding and completes recovery synchronization/barriers before reporting durable readiness.

## Fully retained publication

Validate sealed builder, common account, supported extent, scope/cursor and expected base against actual selection. Acquire fixed publication protection and full `{L + 304, 3, 1}` disk admission. PublicationData holds immutable image owner, fixed records and names, descriptor, permit, cancellation flags and store/session references. Allocate all three file controls and preparations before returning runnable work.

```text
Three-name absence preflight
  → Seal lease → Seal image → Seal temporary descriptor
  → Replace selected → DirectorySync
  → Settle actual selection and ledger
```

Preflight uses no-follow inspection of all canonical names under stable locking and active protection. Any existing entry rejects the generation before ownership proof creation. Retain the prepared descriptor privately, then issue final Replace and DirectorySync directly from the outer operation, avoiding a late ImagePublication allocation.

Healthy cancellation finishes the currently started preparation through sealing and native close, then stops before the next preparation or Replace. Before any Create acceptance, release admission only after read originals settle; if inspections marked the permit started, explicitly settle zero effects. Once a preparation finishes, the exact durable prefix can settle: lease alone, lease plus image, or all three files. Release unused future capacity only after those preparations can no longer start. The unselected owned family then becomes eligible for GC.

After Replace acceptance, cancellation cannot skip the directory barrier. Genuine filesystem errors drain required cleanup, fence and retain conservative reservation debt. Version one repairs through close/reopen and census, rather than an in-place reconciliation subsystem. A failed Create never proves absence.

Successful final DirectorySync records/protects actual selection and settles exactly once in retained state even after facade cancellation or close. Credit the old selected descriptor's `{176, 1, 0}` only when it existed. Check settlement arithmetic before Replace; an internal inconsistency must still preserve knowledge of the successful new root while fencing further mutation. Publisher advancement only updates acknowledgement/results, and the next publication must supply the actual current base.

## Reusable reclamation and shutdown

Use eight fixed family slots and one forward cursor, preserving it between slices of at most 256 inspected entries. Schedule publications between completed GC batches; do not restart the directory scan per candidate. Check roots/protection, open a fresh lease control, acquire EX|NB and read its proof while retaining the lease. Copy decoded proof into the fixed batch slot and close temporary descriptor scratch before another record.

Validate the whole family before its first deletion. Check safe metadata and image/descriptor extents against the supported declaration; preserve unsafe, conflicting, unsupported and unprovable families and unknown suffixes. A valid full temporary descriptor supplies an additional binding check. Ownership proof can cover supported interrupted payloads within bounds; normal GC never hashes image bodies.

Today's image admission limit does not limit ownership of older families. Reclamation checks the supported ownership codec and declared extent independently of recovery/publication quotas. Every available byte of a partial ownership-bound field must agree, including lengths, canonical offsets derivable from complete counts, string extent, semantic/reserved fields and capabilities. Fully present identities must be nonzero. A complete image header also binds temporary descriptors to its cursor/profile.

```text
Validate batch; hold and sync every EX lease
  → Remove observed payloads → DirectorySync
  → Remove lease names → DirectorySync
  → Close every lease → Credit observed deletion once
```

Recheck protection before deletion. Credit captured actual footprints, with presence and completion bits preventing duplicate credit through repeated enumeration or idempotent absence. Cancellation before deletion closes the batch without credit. After deletion begins, finish the validated batch and both barriers despite cancellation. Genuine failure stops further destructive work, settles originals, closes handles, preserves debt and fences.

Close revokes new work and requests termination at these publication/GC boundaries. Existing tokens drive required barriers and closure; a parked cursor receives authentic CloseCursor before claim release. The worker coordinator retains the store through every accepted outer operation and closes all collector handles before releasing operation, collector and session roots. Destructors perform no blocking shutdown; abandoning an undrained worker-owned store is a lifecycle violation. Managed `drained()` reports settled work and closed controls; physical backend/domain drainage can still wait for external aliases.

## Implementation order and acceptance

1. Complete CreateInto and compatible artifact-mode support.
2. Implement StoreCore, bootstrap commitment and composed startup through exact census and durable readiness.
3. Implement fully preconstructed publication and retained selection/account settlement.
4. Implement reusable GC slices, both batch barriers and cursor-preserving scheduling.
5. Integrate managed ImagePublisher and complete close/drain handling.
6. Qualify the entire component and obtain one consolidated source review.

| Area | Required evidence |
| --- | --- |
| Admission | Fail every allocation before family effects; collectors progress under account saturation. |
| Bootstrap | One-unit-short root quotas prevent owner creation; interrupted bootstrap, legacy evidence, corrupt identity and over-quota census. |
| Publication | Independent filesystem census matches first/replacement accounting; every cancellation boundary preserves actual selection and exact prefix settlement. |
| Faults | Before/after Create, short writes, sync, Replace, unlink and both barriers preserve debt and originals. |
| GC | Eight simultaneous exclusive leases, pinned readers, repeated enumeration, partial families, quarantine and exact-once credit. |
| Shutdown | Facade loss, parked cursor, held originals, revoked identities, delayed aliases and weak controls preserve worker lifetime. |
| Recovery | Real process kills around metadata/family sealing, selection and both GC barriers, with independent recovered census. |
| Scale | Growing directories demonstrate bounded admitted metadata and linear inspection work; report barriers, CPU, logical reclaimed bytes and blocked-reader events. |
| Compatibility | Journal/Xapiand, legacy publisher, dual SDK readers, three native platforms, full regression, ASan/UBSan and focused TSan. |

The scalability check bounds permanent graphs by configured controls, slices and batches. Edge-case analysis requires real original settlement and preserved debt. The dependency/state audit keeps operations out of StoreCore and native ownership behind the validated alias holder. Security review binds deletion to validated ownership and held leases, preserves quarantine and separates actual durability from acknowledgement. These are design conclusions; implementation acceptance still requires the evidence above.

## Qualified creation and sharing prerequisites

The integrated component is being implemented in `disk_store.hh/.cc`. Checks cover bootstrap quotas, saturated-admission startup/reopen, retained first/replacement publication, independent filesystem accounting, shared-lease contention and repeated collection. The managed publisher adapter and legacy publication fixture pass together. Focused component qualification passes on macOS, Linux and FreeBSD and under macOS ASan/UBSan. Allocation failpoint coverage, complete platform/regression/sanitizer gates and consolidated Architect review remain required before component acceptance or production activation.

The expanded fixture passes 44 publication cancellation cases, 24 startup shutdown cases, 40 before/after publication original faults, 156 GC cancellation/fault cases across 39 originals, and 18 real SIGKILL recovery boundaries on all three platforms and under macOS ASan/UBSan. Failures retain conservative debt; reopening obtains exact observed usage. A late cursor-close fault preserves earlier batch credits already proved by both barriers and lease closure. Process kills exercise namespace recovery and released kernel locks, not power-loss durability. Subsequent startup-profile coverage is being qualified separately.

`build/tests/detent_local/test_local_disk_store --scale 100000` provisions 100,000 real empty foreign files alongside nine managed generations. An initial Intel macOS warm-cache run retained 13,152 bytes, used one scan and 100,081 entry inspections, read 2,560 record/header bytes and reclaimed 3,904 logical bytes in 0.742031 wall seconds and 0.393883 CPU seconds. Adding fixed per-family cursor/profile evidence for descriptor binding raises the measured macOS capacity to 14,048 bytes; the dense fixture then observes eight held leases and two barriers. The 100,000-file qualification with this additional evidence records:

| Platform | Managed bytes | Scans | Entry inspections | Barriers | Peak leases | GC wall seconds | GC CPU seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Linux VM, ext4 | 13,904 | 1 | 100,081 | 16 | 1 | 0.262483 | 0.223466 |
| FreeBSD VM, UFS | 13,960 | 1 | 100,085 | 2 | 8 | 0.100992 | 0.093750 |

The corrected-source fixture, including recovery/proof synchronization and complete partial-field checks, also passes the real 100,000-file workload on all three platforms:

| Platform | Managed bytes | Scans | Entry inspections | Barriers | Peak leases | GC wall seconds | GC CPU seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Intel macOS, APFS | 14,048 | 1 | 100,081 | 16 | 1 | 0.874944 | 0.381184 |
| Linux VM, ext4 | 13,904 | 1 | 100,081 | 16 | 1 | 1.702830 | 0.244486 |
| FreeBSD VM, UFS | 13,960 | 1 | 100,085 | 2 | 8 | 0.109158 | 0.093750 |

These qualification runs overlap other host IO tests. Each reads 2,560 bytes and reclaims 3,904 logical bytes; the dense fixture separately confirms eight simultaneous leases. Retained fixture capacity includes its completed publication graph and image buffer. The figures establish linear inspection work and fixed controls for this directory workload, not production latency, platform rankings or cost/power savings.

Both runs read 2,560 bytes and reclaim 3,904 logical bytes. Directory enumeration order changes batch density: scattered eligible families flush in separate slices, while adjacent families share barriers. These are separate warm-cache fixture runs on different environments, not a platform performance comparison. Accounted capacity excludes the runtime baseline, native completion queue, process RSS and separate recovery/mapping resources. The synchronous fixture establishes bounded metadata and linear inspection work; it does not measure production completion-loop latency or establish a cost/power improvement.

Cluster `3636009` adds preadmitted exclusive creation and closed-control preparations. Eleven standalone suites pass on macOS, Linux, FreeBSD and macOS ASan/UBSan; focused creation/coroutine TSan and three-platform journal compatibility also pass. The module README records fixture capacity and object sizes, including the native queue increase from 896 to 912 bytes. Legacy journal bytes and wire formats remain unchanged; consumers rebuild against matching headers.

The owning-session factory preserves its existing four-argument form and adds an explicit artifact-mode overload. Native tests on all three platforms verify newly created `0640` and `0644` artifacts while newly created owner locks remain `0600`. The fixture provisions its directory as `0700`, independent of the test runner's umask. macOS ASan/UBSan and focused fully instrumented TSan pass. Sharing policy still requires appropriate deployment directory traversal permissions.

Architect clarification: introducing metadata during a legacy upgrade does not convert existing empty leases. On subsequent managed opens, an exactly empty selected lease remains readable after full recovery and safe held/named validation. Preserve that generation's artifacts and charges when it retires; it has no managed deletion proof. Every new managed publication requires the bound 128-byte record. Malformed or unsupported nonempty leases never enter this compatibility path.

Recovery durability amendment: valid cached contents can survive a process crash between Write and Sync without proving power-loss durability. Before reporting Ready, startup synchronizes verified metadata and, when selected exists, its descriptor, lease and image, then synchronizes the directory. A required file-sync error fences activation. GC synchronizes each validated, exclusively held ownership lease before rechecking names and accepting its first unlink; failure preserves the family and its charges. The native fixture passes the expanded 160 GC cancellation/fault cases across 40 originals. Ten additional managed/legacy recovery cases verify all four selected-file sync failures and successful legacy upgrade, managed reopen and preservation of a retired legacy family.

The consolidated source review found cancellation acknowledgement, quota-coupled ownership validation, incomplete partial-field quarantine and premature startup selection knowledge. Corrections are implemented and the Architect confirmed source GO with no unresolved defect in the reviewed graph. The final focused fixture passes on Linux (8.57 seconds), FreeBSD (8.74 seconds) and Intel macOS (222.21 seconds), including 58 family cases and six publisher cancellation/recovery acknowledgement cases. Fully instrumented macOS TSan passes (226.90 seconds), and the full macOS ASan/UBSan regression passes all 69 tests (536.43 seconds). The macOS timings overlap other durability qualification and are test-run durations, not performance comparisons. The final native regression passes all 69 tests (288.57 seconds). Qualification uses cluster `3636009`; no production activation is implied.

## Application integration contract

Create the store on a startup/validation worker with an application-owned retirement domain, nonnull lifetime admission and explicit logical disk limits. `FreshEmpty` can create the stable owner lock; the other profiles require it to exist. The factory performs locking, entropy and full selected-image recovery synchronously before returning completion-driven startup work. Runtime publication and reclamation expose bounded IO requests; full image construction and validation belong on their respective workers.

One executor owns coordinator calls. Retain the store and each entire returned `operation()` through accepted original completion. Submit through the configured completion backend, call `complete()` only with the authentic original result, retire the preceding request/result aliases before reusing controls, and call `advance()` to schedule subsequent work. `request_collection()` coalesces requests; attempt any pending publication between completed batches before advancement resumes the parked cursor. There is no background timer or self-started IO in this library.

Use `selection()` for actual durable base selection and `ImagePublisher::published_cursor()` for acknowledged publication. They can differ until publisher advancement, and uncertainty forbids another publication. On shutdown, retain and drive the store through `close()` and `drained()`, then release external aliases and close/wait for the retirement domain on the shutdown worker. Managed drainage settles operations and closes handles; the runtime can retain its charged domain control through the end of its current registry pass, so tests wait separately for actual account release. Abandoning an undrained store does not perform destructor-driven shutdown.
