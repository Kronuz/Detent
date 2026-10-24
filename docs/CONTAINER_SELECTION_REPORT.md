# Model container selection: report

**Date:** 2026-10-24. **Branch:** `track/container-selection` (base `main` at `80d4071`).
**Status:** The reconciled candidate adopts the shared `Kronuz/flat-map` engine. Clean public-pin native qualification passes; Linux native and sanitizer CI pass. Final macOS CI is pending. The earlier private-container results and baseline comparison remain attributed below.

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

## Contributor baseline comparison

This table preserves the comparison reported at `19cffc5`. It distinguishes baseline failures from evidence about the container change; the reconciliation results below describe their later disposition.

The branch adds the shared C++ CI. To separate pre-existing failures from this change, `track/ci-baseline` runs the same workflow on `main` (`80d4071`) with only the CI commits. Linux jobs use GCC 13.3 and Clang 18 on Ubuntu 24.04.

| Linux job | Baseline (`main`) | This branch, attempt 1 | This branch, attempt 2 |
| --- | --- | --- | --- |
| GCC x86_64 | 500K relay timeout | pass, 211/211 | pass |
| GCC arm64 | pass, 207/207 | 500K relay timeout | 500K relay timeout |
| Clang x86_64 | pass, 207/207 | two 500K timeouts, `test_managed_delivery_process` | pass, 211/211 |
| Clang ASan/UBSan, LeakSanitizer on | `test_agent_cli`, `test_agent_http`, `test_system_authority_writer_failover` | `test_agent_cli`, `test_agent_http`, `test_local_managed_recovery_failure` | `test_agent_cli`, `test_agent_http`, `test_distribution_encoded_update` |

The comparison identifies no reproducible failure introduced by the container change. It does not close the pre-existing release failures:

- **`test_agent_cli` and `test_agent_http`** fail under ASan in every run, including the baseline: the agent daemon exits before reporting readiness. This is pre-existing.
- **The 500K relay-paged tests** time out on some runners in both arms. They pass in 26 s on the development host.
- **Each remaining failure occurs once** and passes in another run of the same commit. `test_local_managed_recovery_failure` also passes 200 of 200 local runs on both trees.
- **Every container test passes on every Linux job**, including `test_model_alloc_failure`, `test_client_publication_failure`, `test_flat_map` and `test_duplicate_precedence` under ASan with LeakSanitizer.

Two CI fixes apply to `main` as well: `.scratch/` is ignored (the system tests write evidence there, and without the rule `git status` exceeds their 64 KiB limit), and macOS builds with Homebrew LLVM for `std::jthread`. macOS still fails to build in both arms: with Homebrew LLVM 23, the `compressors` dependency places the SDK's C headers ahead of libc++'s, which libc++ rejects. That run leaves the include-order problem open; the macOS arm64 suite passes 211/211 on the development host. Reconciliation uses explicit SDK selection in the shared workflow (`8459784`), and both hosted macOS jobs now build and run tests. Their final results remain pending.

## Compatibility

The four public field types change from `btree::map` to `detent::flat_map`. Enclosing layouts, iterator types and invalidation rules change: any insert or erase invalidates every iterator. Keys are not const, and callers must not modify them. Erasing entries one at a time in a loop is quadratic; use `erase_if`. Every C++ consumer must be rebuilt together. Serialized, durable and shared-memory bytes are unchanged, and Detent no longer depends on cpp-btree.

## Gaps and pending work

1. **macOS CI.** Build-stage toolchain and SDK mismatches are corrected in reconciliation; the final test results remain pending. The older GCC 11 development VM does not establish qualification of the current supported toolchain profile.
2. **GCC 11 sanitizer loop.** `test_model_and_schema` and `test_eval_semantics` spin on `AddressSanitizer:DEADLYSIGNAL` under GCC 11 on the VM, an AddressSanitizer issue with high address-space randomization; reruns with randomization disabled are pending.
3. **Pre-existing failures and flakes.** The baseline comparison below records failures across both arms. Reconciliation fixes the empty-publication sanitizer abort, snapshot-plan admission wait and mapped-admission owner-pressure isolation; broader sustained-load qualification remains open.
4. **Not measured.** Allocation-denied authority cleanup end to end, RSS, the 1M, 5M and exploratory 10M capacity tiers, durable publication timing and Linux timing. Python and Rust binding validation passes in reconciliation; this is functional coverage, not performance qualification.
5. **btree library.** The [architecture recheck](reviews/container-extraction-architect-2026-10-24.md) rejects the const-key view at `44cb140` as a portable lifetime repair. Keep that prototype outside adoption; the comparison supplies no safe older rollback point. The independent btree repairs remain upstream. This decision does not require resolving its separate compiler and sanitizer investigations.
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

## Reconciliation qualification

1. The clean native public-pin build passes 224/224 tests (786.80 seconds). Its 35 populated dependency repositories match their immutable pins, are clean, and use no source overrides. The final two isolated gate fixes pass all six affected checks; the two pressure fixtures each pass 100-run checks.
2. Rust's rebuilt shim passes five integration tests. The existing Python binding passes all five checks on Python 3.15 against the rebuilt core. These checks do not imply compatibility with unrecompiled C++ consumers.
3. Canonical JSON and mapped payload bytes match the contributor engine and shared engine. SHA-256: JSON `34ad7707bb440ce49e2b0d67a140cfd32d2d8fd1dc25aab8af7b9c150cdbe403`; mapped payload `ddfe90b02e5223fa8bd66151648bd0a74cfa648c668316bd1b0bcb951f387b1b`.
4. Product CI run [40098546698](https://github.com/Kronuz/Detent/actions/runs/40098546698) passes Linux GCC x86/ARM, Clang x86 and ASan/UBSan with leak checks. Both macOS jobs fail strict TLS verification because generated CA certificates contain duplicate extensions; the hermetic fixture correction is `6bf4e96`. The product workflow explicitly enables relay; this profile registers 216 cases with benchmarks disabled, while the local full profile registers 224.
5. The empty source-only startup failure is a null `memcpy` source for a zero-byte payload, identified from the newly exposed detached-child log and fixed in `e882fcc`. The snapshot pressure test used an aggregate threshold greater than its entire heap; `07a8a41` waits for actual admission with pressure retained. `a23ca07` isolates and drains each mapped-recovery pressure domain.

The full 224-case run identifies source `07a8a41`; final affected checks identify `a23ca07`. The later contributor updates change report content and duplicate existing CI setup, so reconciliation preserves the already-tested production and workflow blobs. Scope, counts and pending platform/capacity gates remain explicit.

## Hosted qualification follow-up

[Run 40117311416](https://github.com/Kronuz/Detent/actions/runs/40117311416), source `6bf4e96`, passes Linux Clang x86 and macOS ARM. Linux GCC x86 fails with `bind: Address already in use` in the legacy Raft fixture. GCC ARM times out in 500K migration after 300 seconds; ASan/UBSan times out in selected-sink cancellation after 90 seconds. macOS x86 remains pending. These failures remain qualification blockers.

The Raft fixture now reserves OS-selected TCP ports until listener startup and binds the advertised peer endpoint. Ten repeated local cluster runs pass in 45.20 seconds. The original fixture selected an unrelated random listener port after configuring its advertised address. The reservation-to-listener handoff still requires closing the reservation because the HTTP service does not accept an existing acceptor.

The 500K migration check passes locally in 38.28 seconds; selected-sink cancellation passes 100 local repetitions. Neither result attributes or resolves the hosted timeout. Coordinator checkpoints expose setup, TLS acceptance and drainage phases without increasing deadlines or weakening assertions.

The standalone SDK consumer fixture passes after project-relative source path correction `875de05`. Standalone product compile commands remain byte-identical. This check covers embedding through CMake FetchContent, independently of the already-qualified Rust and Python shims.
