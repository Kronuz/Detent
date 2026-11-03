# Resource adoption

> Status: ACTIVE (2026-11-02) Detent adopts the shared resource primitives in `allocators` by owner ruling; see [the decision](../SHARED_LIBRARY_DECISIONS.md#resource-accounting-one-shared-implementation-november-2).

## State

Branch `phase/resource-adoption`, from Detent:6a31280. The code is the resource part of `track/resource-adoption` (Detent:39f19a4, benchmark sources from Detent:6427b5e), reconciled onto current main.

## Decisions

1. Authority retained-state funding (Detent:33d36bf `src/detent_authority/*`, `tests/detent_authority/test_{node,service}.cc`) is not taken: it needs `consensus:61149a2` (`WorkerLimits::retained`) and is a separate pin wave and feature; the extraction architecture also excludes it from EX-1.
2. Pins: `allocators:07de45d`, plus `completion:21809c7` and `journal:dbf753f`, the immutable closure the extraction architecture names (their allocation and reservation names forward to allocators). `stash` and `lru_cache` keep main's pins.

## Done

1. Adopted allocators (`ByteAccount`/`ByteCharge`, charged allocator/PMR resource, `OwnedBuffer`, retirement, `ReservationBook<3>`) behind Detent's existing names: `include/detent_distribution/{storage,allocator,buffer,memory_resource,retirement}.hh` are aliases; `DiskLedger` keeps its census, serial/slot registry, original-operation counts and proof rules and delegates committed/reserved arithmetic to the book; `detent_resources` is an INTERFACE target over `allocators::resources` and `allocators::retirement`. Detent:6618799 (pins and code), Detent:43e09fe (deletes `src/detent_distribution/retirement.cc`, adds the first-read controller).
2. Lines: 61 added, 1,046 deleted in `include/` and `src/` (8 files, including the 384-line `retirement.cc`).
3. Benchmarks `benchmarks/resource_{ownership,retirement}_bench.cc` come from the track branch (Detent:6427b5e); they exercise the shared types.

## Evidence

Tests (a mutant, sanitizers, native):

| Check | Result |
| --- | --- |
| Native Release, macOS x86_64, authority+relay+managed agent | 235 registered, 234 pass, 1 privileged fixture skips, 0 fail, 916.28 s |
| ASan+UBSan, Linux x86_64, clang 18, LeakSanitizer on | 227/227 pass, 82.8 s. (The macOS-only tests are not registered on Linux.) |
| TSan, Linux x86_64, clang 18 | 218/227 pass; 7 not run and 2 fail, all identical on main (below) |
| Mutant | `DiskLedger::settle` credits `{}` instead of `durable_credit`: `test_local_disk_ledger` fails ("disk ledger invariant failed") and passes unmutated |

The new tests are the existing storage, retirement, retirement-factory, memory-resource and disk-ledger suites; the branch adds no new test source. No fix is added, so there is no revert check beyond the mutant.

TSan notes: the 7 not-run tests (`test_model_alloc_failure`, `test_client_publication_failure`, `test_local_client_source`, `test_local_disk_ledger`, `test_local_directory_ownership`, `test_local_inventory`, `test_http_socket_ownership`) define their own `operator new` and do not link under TSan; hosted CI disables TSan for the same reason. `test_file_source` (race in `Client::~Client`, `src/detent/client.cc:148`) and `test_local_paged_collection_crash` (TSan refuses threads after fork) fail identically on a main build (Detent:6a31280). `test_storage`, `test_retirement` and `test_retirement_factory` pass under TSan.

Environment: the Linux box needed `umask 022` (the trusted-ancestry check rejects group-writable parents) and `ulimit -n 65536`; with those the same build passes.

## First reads

Probe `detent_system_prepared_client_probe --read`, the prepared managed 5M corpus (copy of `managed-5m-r3/generations`), 40 recorded keys, a separate warmup process per measured process, arms main/migrated/main again rotated per key and round, `quiet strict` holds, `benchmarks/resource-qualification/first_read_ab.py` and `cpp-libraries/tools/ab_compare.py`. Both binaries are Release builds of the same compiler and flags (main is Detent:6a31280).

| Batch | main | migrated | A/A (main again) | paired median first read, migrated minus main | A/A paired |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10 rounds (400 samples/arm) | 793.3 us | 796.7 us | 1.009 ratio | +6.5 us | -3.1 us |
| 16 rounds (640 samples/arm) | 817.5 us | 822.1 us | 1.005 ratio | +8.2 us | +0.3 us |

ab_compare calls every row noise (per-run geomean 0.995 and 0.987; A/A 1.008 and 1.002). Below 1 ms: 358/357/367 of 400 (10 rounds, machine busy) and 588/575/596 of 640 (16 rounds). Process CPU adds a paired median of 10 us and minor faults add exactly 2 (mean 85.63 vs 83.54, A/A 83.64) in the 16-round batch. The earlier counterbalanced result was +21.9 us.

Attribution (counts, no timing): per-phase minor faults, 100 interleaved rounds (median, main / migrated / main again): factory 5 / 4 / 5, Client construction 57 / 60 / 57, first lookup 17 / 21 / 17; total 79 / 85 / 79. Small-heap blocks in use at the end: 231 vs 235 (+4 blocks, +272 bytes, none of them in the lookup). `vmmap` after the same read: resident `__TEXT` 5,160 KB vs 5,176 KB (+16 KB) with identical `__TEXT` size (581,632 bytes; `__text` +1,088 bytes) and no difference in any data region beyond allocator-arena noise. So the added work is first touches of about four more code pages, not heap growth or an extra algorithm: the shared accounting templates (`ByteAccount`/`ByteCharge`/`ChargedAllocator`, 410 `allocators::` text symbols in the migrated probe against 150 equivalents on main) are instantiated per type and laid out across more pages on the lookup path. I cannot name a line without per-page residency tracing (no Xcode `xctrace`, no root for dtrace).

Where it lives: in the layout of header-instantiated allocators code in Detent's binaries, not in an allocator algorithm and not in Detent's use of it. Proposed fixes, none applied: (1) in allocators, move the hot out-of-line paths of `ByteAccount::try_charge`/`ByteCharge` release and the `ChargedAllocator` allocate/deallocate bodies into one non-template function each, so there is one copy in one page range; (2) in Detent, an `-order_file` (or hot-section) for the reader link so the lookup path's text is contiguous. Either needs its own measurement; the gap is about 1 percent of a median first read.

## CI

Main's reference run 40950833416 (Oct 29): `linux-x86_64-gcc` fails; `linux-asan-ubsan`, `macos-arm64`, `linux-arm64-gcc`, `linux-x86_64-clang` and `macos-x86_64` pass; `freebsd-x86_64` and `linux-tsan` are disabled. A rerun of its `macos-arm64` job also passes.

| Run | Commit | asan-ubsan | macos-arm64 | macos-x86_64 | x86_64-gcc | x86_64-clang | arm64-gcc |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 41568242189 | Detent:43e09fe | pass | fail, then fail on rerun | pass | pass | pass | pass |
| 41573859950 | Detent:f552f29 | pass | pass, pass on two reruns | pass | pass | pass | pass |

The `macos-arm64` failure is `test_local_paged_segment_collection` aborting in 0.10-0.15 s with `snapshot invariant failed` (an unnamed `require`). Detent:f552f29 only adds the line number to that message (test fixture); the same product code then passes three times. It does not reproduce on macOS x86_64 (600 runs, 24-way parallel, plus 200 and 200 at 4-way, for the migrated build; the main build was run at 4-way only) nor on Linux ASan/UBSan (227/227) or Linux arm64 gcc. Main's `macos-arm64` job passed twice. So: an intermittent arm64-only failure seen on the migrated branch in 2 of 4 attempts and not on main in 2 of 2; cause unknown. The next failure will name the line.

## Next

1. The lead decides on the allocators change (1) above.
2. If `macos-arm64` fails again, the message now carries the failing line in `tests/detent_local/test_paged_segment_collection.cc`; start there (the test exercises `RetirementRuntime`, the domain residents drain, and `DiskLedger` counters).

## Blocked on

The intermittent `macos-arm64` failure above needs an arm64 macOS host or the line from the next failing run; adoption should not land until it is explained.
