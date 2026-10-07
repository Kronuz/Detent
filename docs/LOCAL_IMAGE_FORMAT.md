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
