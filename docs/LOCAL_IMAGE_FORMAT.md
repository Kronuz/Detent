# Local image format, version 1

The Stage 4 local image implementation has a dynamically sized, immutable byte region and a separate selected-generation descriptor. It is available through `detent::local_view` and `detent::local_builder`; the existing agent and CLI still use their legacy shared-memory path until publication and refresh integration is complete.

## Encoding

All integers are explicitly encoded in big-endian order. The image contains a 192-byte header, 24-byte namespace rows, 48-byte record rows, 24-byte override rows, and a dense string region. Region offsets are canonical and checked with overflow-safe arithmetic. The default image admission ceiling is 256 MiB, with independent limits of 500,000 records, 4,096 namespaces, 128 MiB of strings and 256 overrides per definition. These limits describe image bytes and logical counts; they do not establish an aggregate construction-memory budget.

The header binds the complete scope Cursor and semantic Profile. Namespace rows carry their revision watermark. Records carry their namespace index and key revision, followed by the complete boolean or integer definition. Override rows encode a forced boolean or a rollout percentage. Namespaces, records and each definition's overrides must be strictly ordered and unique. Every string is consumed exactly once in canonical row order. Reserved fields must be zero.

The 176-byte `DTSELECT` descriptor contains its format version, a nonzero 16-byte generation identity, image length, SHA-256 checksum, complete Cursor and Profile. Filenames derive only from the hexadecimal generation identity: `generation-<32 hex digits>.image` and its separate `.lease` file. Knob names never become filesystem paths. The descriptor is named `selected`.

## Validation and ownership

`ImageValidator` retains an opaque immutable-byte lease. It charges row and byte budgets before validating each row and exposes a `ReadView` only after all regions and definitions validate. Namespace watermarks cannot exceed the Cursor revision, and key revisions must be positive and cannot exceed their namespace watermark. An optional expected Cursor requires the same identity and a revision at least as recent.

`open_generation` runs on startup or a refresh worker. It opens the directory and descriptor without following symlinks, checks regular files and ownership, and acquires a shared nonblocking kernel lock on the separate generation lease before opening the image. A lease unlinked during acquisition causes a bounded retry. Files must have exactly one link and cannot be writable by group or other users. The configured runtime directory and its ancestry must be trusted; this function does not authorize the publishing service account.

The reader checks the exact file length and descriptor checksum, validates all rows, and retains the mapping, image descriptor and shared lease descriptor through every copied `ReadView`. Cleanup must acquire an exclusive nonblocking lock on that same lease file before removing a retired image. The publishing service must never modify or truncate a sealed image. The reader currently performs startup/refresh work synchronously on its caller; integration must keep it off application lookup threads.

`ReadView` performs binary searches over validated rows and observes one complete generation throughout its lifetime. Its lookup path has no explicit filesystem or network calls. Boolean evaluation preserves the existing full-name SHA-256 rollout semantics, including forced switch precedence and per-switch rollout percentages. The shared hash helper streams the full knob name, NUL separator and hash value directly into SHA-256. In the [allocation benchmark](../benchmarks/hash-allocation-measurements.json), 100,000 calls with a 128-byte hash value change from 100,000 input-buffer allocations to zero while preserving the result sum. Three elapsed-time samples overlap; this establishes allocation removal, not a service latency or power improvement. Error results own diagnostic strings.

`ImageBuilder` borrows a `ValidatedSnapshot` through a shared owner. Counting, buffer initialization, row construction and integrity hashing advance through caller-supplied budgets. The output and descriptor become available only after sealing. It currently reserves a complete image buffer; physical allocation admission, accepted-original retention, publication uncertainty and retained-generation accounting belong to the pending publication integration.

## Verification and remaining gates

The local layout, view and builder suites cover dynamic images above the former 4 MiB limit, integer extremes, malformed metadata and rows, scope rejection, one-byte work budgets, rollout evaluation, retained view lifetimes, checksum corruption, and exclusive cleanup exclusion while copied views retain a shared lease. The three suites pass on native macOS, Linux and FreeBSD, and under macOS ASan/UBSan. The Linux fixture uses explicit private permissions rather than inheriting a group-writable umask.

The publication pipeline, actual agent/SDK refresh integration, process-kill recovery, cleanup races, exact-base delta construction, independent schema interoperability and representative large-image measurements remain open. The complete [Stage 4 acceptance gates](DISTRIBUTION_ARCHITECTURE.md#whole-component-acceptance-gates) still apply; these reader tests do not establish production or fleet readiness.

## Construction measurements

The [local image benchmark](../benchmarks/local_image_bench.cc) forms 500,000 ordered definitions in one namespace: one quarter integers and three quarters booleans with both a global percentage and a per-switch percentage. The [three fresh-process samples](../benchmarks/local-image-measurements.json) produce a 14,233,697-byte wire object and a 41,000,219-byte local image. Image construction takes 296–304 ms over 119,383 budgeted turns; the largest observed turn is 571 µs. Validation takes 84–93 ms. Across 100,000 preconstructed keys permuted through the 125,000 integer records, lookups take 143–145 ms in total.

Peak RSS is 139.6 MB, including the retained wire object, decoded definitions, complete image and prepared query strings. This corpus differs from the codec comparison workload, so its RSS is not a before/after comparison. The benchmark excludes mapping, publication durability and cleanup. It does not establish sustained refresh throughput, tail latency or an aggregate retention budget.

## Publication implementation

`ImagePublisher` owns the stable backend owner lock and retains the caller's opaque admission lease through accepted completion jobs. It prepares the empty generation lease, immutable image and descriptor in that order, using `ImagePreparation` for each file. It then uses `ImagePublication` to replace `selected` and synchronize the directory. The confirmed published Cursor advances only after both publication originals succeed. A failure after publication submission fences the publisher, preserves potentially selected files and requires recovery.

The native and macOS sanitizer publication suites, Linux and FreeBSD suites inject failures at each of the thirteen original IO steps for a small image. A post-replacement directory-sync failure leaves the complete new image readable while retaining the previous confirmed Cursor and fencing subsequent publication. A held original retains the owner lock and admission lease after the publisher object is destroyed. These tests use synchronous driving of the same operations that native completion owners will accept; they do not qualify process-kill recovery.

Publication is currently built only when `kronuz::completion` is available from reviewed cluster sources. It does not add that dependency to the reader target. Agent-only dependency wiring, actual capacity admission, generation cleanup and integration remain open.

`RecoveredGeneration` is minted only by the file reader. It binds the validated view and exact descriptor to a retained storage directory descriptor and its device/inode identity. A recovered publisher requires that identity to match its POSIX backend; a copied descriptor from another directory cannot substitute for validation of that directory's image. Recovery starts with no confirmed Cursor. Original completion jobs reopen `selected`, check its exact size and descriptor equality, and synchronize the directory before confirming the recovered Cursor. Every recovery error fences the publisher. Starting without a recovery capability while `selected` exists is rejected.

Recovery tests inject failures at each of its four original steps, reject a stale capability after `selected` changes, and reject a descriptor copied into a different directory without the referenced image. The existing agent/SDK path has not yet been connected to these operations.

Recovery assumes generations were created through the sealed publication protocol: image and descriptor content passed their file-sync barriers before descriptor replacement. Repeating DirectorySync confirms the namespace outcome after uncertain replacement; a checksum-valid arbitrary file cannot prove that publication history.

Closing or cancelling an active recovery suppresses Cursor confirmation. Each accepted operation retains its backend, owner lock, storage capability, reader lease and admission through terminal settlement. Closing also prohibits future publication. Cancellation before an original is submitted stops that job; after replacement submission, the publication operation settles its required barrier but confirmation is suppressed and recovery is required before another candidate. Driver submission, cancellation and coordinator advancement must be serialized on the owner lane.

The final publication suite holds each recovery original across destruction, tests close immediately before and after its final directory-sync completion, and tests cancellation around descriptor replacement. It passes on macOS, Linux and FreeBSD and under macOS ASan/UBSan. The complete native build and all 55 registered tests pass with this component. Actual resource admission and leased generation cleanup remain open.

## Storage admission foundation

`distribution::StorageLedger` grants move-only byte tickets before allocation. `PagedStorage` keeps text and future flat rows in stable pages with intrusive metadata, charging each full page (including its header and alignment) until the allocation is freed. Allocation failure rolls back the ticket. Retirement releases one page per call and forbids further allocations once it begins. The single-owner ledger must outlive its pages and outstanding tickets.

The storage test covers quota exhaustion before upstream allocation, allocation failure rollback, stable addresses across page growth and ownership transfer, alignment, arithmetic overflow, and incremental refunds. This mechanism is not yet connected to the decoder, framing buffers, image builder or completion jobs. Its metric covers requested upstream capacity; allocator metadata, allocator caches and whole-process RSS remain separate measurements. Disk admission and lease-protected generation cleanup remain pending.
