# Model container selection: report

**Date:** 2026-10-24. **Branch:** `track/container-selection` (base `main` at `80d4071`).
**Status:** `detent::flat_map` selected and implemented. macOS qualification complete; Linux qualification runs in CI on this branch.

## Summary

Detent's four public model tables (`Snapshot::knobs`, `SnapshotPatch::knobs`, `BoolDef::overrides`, `OverrideTable::values`) used `btree::map`. Its relocation code copies const string keys while reshaping a node, so a refused allocation in the middle of a split, rebalance, merge or erase corrupts the table, leaks, or double-frees. Every revision of the library since its 2019 import fails this way with names longer than the small-string buffer.

This branch replaces those tables with `detent::flat_map`, a sorted vector owned by Detent. Against `std::map` and against btree repaired to relocate by move, it retains the least memory (**14.4 MB** per 100K-knob snapshot with short names, against 17.0 and 24.8 MB) and uses the least lifecycle CPU in every profile, with identical output bytes. The branch also fixes a client bug: a refused allocation during publication left the patch base one version ahead of what readers saw.

## Defects on the actual types

The allocation-denial sweep ([`benchmarks/btree-repair/actual_type_sweep.cc`](../benchmarks/btree-repair/actual_type_sweep.cc)) builds each table, refuses one allocation position at a time during insert, erase and copy, then checks contents, ordering and exact allocation balance. Short names are 9 bytes; long names are the legal maxima (200-byte knob names, 128-byte group keys).

| btree revision | Result with long names | Platform |
| --- | --- | --- |
| `35ac0ec` (2019 import), `3e9f417` (2021) | insert 3,872 violations, erase 1,271 (segfault), copy 13,408 leaks | macOS, max_n 12 |
| `dd984d9` (Oct 19) | insert 2, erase 29 (key corruption), copy 13,408 leaks | macOS, max_n 12 |
| `cbc3576` (production pin) | over 500 wrong-size violations after a refused nonfull insert | Linux ASan, max_n 12 |
| `25f9494` (best revision) | `Snapshot::knobs` insert 41, `BoolDef::overrides` insert 132, erase double-free in all four tables; copy clean | Linux ASan, max_n 40 |

Short names pass on every revision. Further defects at `25f9494` with long names:

- `parse_source_snapshot` leaks on a 40-knob snapshot when allocation 98 is refused.
- Positional erase allocates: 52,007 allocations to prune half of a 100K snapshot, and about 509 per drain of a 256-override definition. The four authority drains run inside `noexcept` functions, where a refused allocation terminates the process.
- `params::swap` writes through `const_cast` to const keys, which is undefined behavior. No miscompile is observed.

## Alternatives

| Candidate | Outcome |
| --- | --- |
| Roll back btree | Rejected. No revision passes; see the table above. |
| `std::map` | Safe (0 violations, erase never allocates) and kept as an intermediate step, but it costs the most memory and CPU. |
| Hash index for `Snapshot::knobs` | Not pursued. The October 22 [comparison](../benchmarks/model-container-comparison-20261022.json) retains 28.8 MB / 90.4 MB and speeds broad hits 44-60% over `std::map`, but it has no adversarial-key policy and needs sorted publication. |
| btree repaired to relocate by move | Correct on every measured configuration (branch [`Kronuz/btree` `move-relocation`](https://github.com/Kronuz/btree/tree/move-relocation)); second in memory and CPU. |
| `std::flat_map` | Unavailable: C++23, absent from GCC 11 and older libstdc++; Detent targets C++20. |
| `detent::flat_map` | Selected. |

## Measurements

### Two arms: btree `25f9494` and `std::map`

Five alternating pairs at 100K knobs with three overrides each, under an exclusive host hold ([record](../benchmarks/model-container-decision-20261023.json)). `std::map` raises total lifecycle CPU by 57% (ordered short names), 25% (random short), 16.5% (ordered long) and 7.5% (random long). Copy and patch application are 1.8 to 5.5 times slower, and misses 16 to 67% slower. Live heap grows from 17.0 to 24.8 MB (short names) and from 68.4 to 76.2 MB (long names).

### Three arms

Five rounds per workload with a rotating start arm, 120 samples, 8 retried for interference, none rejected ([record](../benchmarks/model-container-three-way-20261024.json)). Output digests and lookup checksums are identical across all arms; canonical JSON and shared-memory bytes compare equal.

| 100K knobs, three overrides | flat_map | repaired btree | std::map |
| --- | --- | --- | --- |
| Live heap, short / long names | **14.4 / 65.8 MB** | 17.0 / 68.4 MB | 24.8 / 76.2 MB |
| Live, copy, patch and history coexisting, short / long | **44.8 / 204.6 MB** | 52.9 / 212.7 MB | 76.9 / 236.7 MB |
| Lifecycle CPU, ordered short | **112 ms** | 142 ms | 221 ms |
| Lifecycle CPU, random short | **132 ms** | 184 ms | 236 ms |
| Lifecycle CPU, ordered long | **371 ms** | 424 ms | 516 ms |
| Lifecycle CPU, random long | **477 ms** | 542 ms | 601 ms |
| Parse allocations, short | **75,005** | 250,013 | 325,004 |
| Copy, short | **1.3 ms** | 2.6 ms | 15.4 ms |
| 409,600 broad lookups, short | **28.6 ms** | 32.8 ms | 64.9 ms |
| Cleanup turn at 256 overrides, p99 | 42 ns, 0 allocations | 83 ns, 0 allocations | 42 ns, 0 allocations |

Lifecycle CPU covers parse, serialize, copy, patch parse and application, diff, compose, lookups and destruction. Two caveats apply:

- **Part of the diff gain is algorithmic.** The flat arm also implements patch application, diff and composition as linear merges, which explains most of its 10x faster diff.
- **Prune is excluded from the totals.** The flat arm ran it as a positional erase loop, which takes about 7.4 s; production paths and the benchmark now prune with `erase_if`.

Through the real `Client` and `ClientReadView` (200K operations), `flat_map` is fastest in 14 of 24 rows and within 3% of the fastest in 22. The two exceptions use a hot 32-key set with short names: rollouts take 46.2 ms against 44.5 ms for `std::map`, and misses 8.9 ms against 8.3 ms for the repaired btree. Long-name integer hits take 126 / 130 / 151 ms.

### Repaired btree, library benchmarks

From the [`move-relocation` note](https://github.com/Kronuz/btree/blob/move-relocation/docs/MOVE_RELOCATION.md): with 200-byte keys, erase allocations fall from 72,692 to 0 and insert allocations from 420,230 to 147,659. `mutation_bench` CPU drops 26% at 32-byte keys and 30% at 200-byte keys, and is unchanged at 12 and 16 bytes. Node memory is identical.

## Correctness of the selected design

`detent::flat_map` stores `std::pair<Key, T>` in one sorted `std::vector`. A `static_assert` requires nonthrowing moves, so shifts and erasure never allocate and insertion keeps the strong guarantee. Copy assignment copies into a temporary and swaps, so a failed copy leaves the target unchanged; `std::vector`'s element-wise assignment would leave a source prefix over the old tail, out of order. `erase_if` is `noexcept`.

- **Bulk construction.** JSON snapshots, patches, override objects and CLI overrides are appended and sorted once. Each error exit first reports a duplicate already seen, so messages and their precedence match one-by-one insertion.
- **Linear merges.** `apply_patch`, `diff_snapshots`, `compose_patches` and `merge_over_defaults` are single passes over two sorted tables.
- **Disposal.** The four authority drains erase the last override, O(1) per budgeted turn, and keep the same charge per turn.
- **Client publication.** Both paths build the merged and published snapshots before committing the patch base with a nonthrowing move. `test_client_publication_failure` fails at allocation position 8 without that change.

| Check | Result |
| --- | --- |
| macOS arm64 Release ctest | 211/211 |
| `test_model_alloc_failure` (all four tables: insert, erase, copy, assignment with an unchanged-target check; parse, copy, patch, diff, compose, serialize) | passes; aborts on btree `25f9494` and on `flat_map` with a defaulted copy assignment |
| Differential probe against the `std::map` tree, 30,000 random rounds (parse, errors, patch, diff, compose, defaults, CLI) | identical |
| Repeated `"overrides"` members, 160K (reviewer probe) | 18.7 ms (5.4 s before the fix; `std::map` 46.7 ms) |
| 160K descending CLI overrides | 6.6 ms (11.9 s before the fix; `std::map` 51.6 ms) |
| Linux GCC 11 ASan with leak checks on the `std::map` step: model failure, client failure, patch, overrides tests | pass, 0 leaks, 0 reports; the `flat_map` tree runs in CI |

Reviews: the [`std::map` step](reviews/container-std-map-code.md) was approved with conditions, and its findings fold into this branch. The [`flat_map` step](reviews/flat-map-code.md) was rejected for one quadratic parse path (CR1); CR1 to CR4 are fixed, each in its own commit.

## Compatibility

The four public field types change from `btree::map` to `detent::flat_map`. Enclosing layouts, iterator types and invalidation rules change: any insert or erase invalidates every iterator. Keys are not const, and callers must not modify them. Erasing entries one at a time in a loop is quadratic; use `erase_if`. Every C++ consumer must be rebuilt together. Serialized, durable and shared-memory bytes are unchanged, and Detent no longer depends on cpp-btree.

## Gaps and pending work

1. **Linux full suite.** The shared development VM has only GCC 11, which lacks `<format>` (required by the `compressors` dependency), so it cannot build Detent. CI on this branch runs GCC 13.3 and Clang 18 on `ubuntu-24.04` (x86_64 and arm64), macOS, and Clang ASan/UBSan with leak checking.
2. **GCC 11 sanitizer loop.** `test_model_and_schema` and `test_eval_semantics` spin on `AddressSanitizer:DEADLYSIGNAL` under GCC 11 on the VM, an AddressSanitizer issue with high address-space randomization; reruns with randomization disabled are pending.
3. **Pre-existing flake.** `test_local_paged_snapshot_plan` aborts about once in six runs on both `std::map` and `flat_map` trees.
4. **Not measured.** Allocation-denied authority cleanup end to end, RSS, the 1M, 5M and exploratory 10M capacity tiers, durable publication timing, Linux timing, and the Python and Rust bindings (they do not name the four types).
5. **btree library.** The `move-relocation` branch still needs its sanitizer matrix, FreeBSD, an unexplained GCC 16 `-O2` abort that also affects `master`, and the g++ leg of its sanitizer matrix. Its sanitized g++ `prepared_commit` spin is GCC 11's AddressSanitizer loop: `master` spins the same way, and both pass with address randomization disabled. Detent no longer depends on it.
6. **`std::flat_map`.** Once every supported toolchain ships it, a switch is a contained change, but it is another public type change and needs its own measurement.

## Reproduce

```sh
# Failure sweeps and tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDETENT_BUILD_AUTHORITY=ON
cmake --build build -j4 && ctest --test-dir build -j4

# Three-arm 100K comparison (two or more benchmark directories)
python3 benchmarks/container-alternative/container_pair.py OUT \
    flat=build/benchmarks btree=../btree-build/benchmarks std_map=../map-build/benchmarks
```

## Shared-library adoption

The reconciled candidate links `flat_map::flat_map` from public revision `a9a52b02e387ab054ff38647d3f141f9f8c00593`. Its seven-job Linux/macOS/FreeBSD and sanitizer matrix passes. `detent/flat_map.hh` forwards names to the canonical implementation; it contains no container engine. Strong assignment and the contributor's failure tests are preserved. The library additionally constrains copy assignment to nonthrowing comparator swap and gives move operations conditional exception specifications. These restrictions preserve the current `std::less<>` model behavior.

This adoption changes canonical C++ type names and requires rebuilding consumers, including bindings. The model comparison remains evidence for the selected representation; it does not measure the later library packaging or establish release-scale capacity. Public-pin composed checks and exact byte comparison remain required before landing.
