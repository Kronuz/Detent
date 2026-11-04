> Status: CURRENT (2026-11-03) How the first-read and lookup counters work. Workstream: reader-s1-s4. Design: [reader.md](../design/reader.md) S1.

# Read counters

Six process-wide counters say what a first read and a lookup cost: `directory_trust`, `record_opens`, `table_bytes`, `segment_opens`, `pages_verified` and `bytes_hashed`. They answer T1 (pages verified per scattered lookup) and T2 (work per first read) without a profiler.

## The switch

`-DDETENT_READ_COUNTERS=ON` defines `DETENT_READ_COUNTERS` for every target (the CMake option is off by default). The code reaches the counters only through `DETENT_COUNT(field, amount)` in `include/detent_local/read_counters.hh`:

- On: it is a relaxed `fetch_add` on a static cell (`counters::cells()`), one atomic per field.
- Off: it expands to `((void)0)`. The argument is not evaluated, no cell exists, and `counters::snapshot()` returns zeros. The off build's objects for every instrumented file match main's (see the ledger).

Every translation unit of a program must agree on the flag, because `Cells` is an inline variable.

## Where each counter moves

| Counter | Moves in | By |
| --- | --- | --- |
| `directory_trust` | `open_publisher_directory` (`generation_file_access.hh`) | 1 per call |
| `record_opens` | `open_record` (`prepared_reader.cc`): `selected`, lease, `storage.meta`, image, `.read` | 1 per open |
| `table_bytes` | `read_generation_record` | the bytes asked for |
| `segment_opens` | `LazyPageAccess::open_mapping` | 1 per segment payload opened |
| `pages_verified` | `LazyPageAccess::resolve`, after `verify_page` passes | 1 per page |
| `bytes_hashed` | `verify_page` (page bytes, then the logical commitment input: 44 + prefix + 33 per child + body), `checksum` in `prepared_reader.cc` (the image), `decode_reader_metadata` (160), the ownership `validate` (record length minus 32) | bytes given to SHA-256 on the read path |

`table_bytes` counts every generation record today (there is no table yet); when the store table lands it counts that read.

## Reading them

`counters::snapshot()` returns a `Snapshot`; `a - b` subtracts two. `tests/detent_system/reader_benchmark.hh` takes a snapshot around each timed loop and, when `counters::enabled`, adds a `counters` object (with `pages_verified_per_lookup`) to each result line; the line is unchanged when the flag is off.
