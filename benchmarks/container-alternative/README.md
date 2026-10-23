# Container alternative probes

The [prototype report](../standard-map-prototype-20261021.json) compares the original pinned btree with an isolated four-field standard-map alternative. It does not select a public C++ API change. Both publication timing attempts are rejected on host interference; their partial samples remain explicitly rejected.

For the retained-memory probe, compile this same source against each checkout's include directory and the pinned cpp-btree headers, using C++20, `-O2 -DNDEBUG`. For example, from a checkout with the pinned headers available at `$BTREE_HEADERS`:

```sh
clang++ -std=c++20 -O2 -DNDEBUG -I include -I "$BTREE_HEADERS" benchmarks/container-alternative/model_footprint.cc -o .scratch/model-footprint
```

The global allocation replacements meter requested live bytes, including string storage, and check zero balance after destruction. Instrumentation headers and allocator rounding are excluded. The source constructs exactly-sized inputs to avoid confusing excess input-string capacity with container storage. It measures short keys and legal maximum-length keys, each with 100,000 knobs and three overrides per knob. Run builds and probes under the workspace quiet protocol.

To reproduce the prototype, apply `standard-map.patch` only in an isolated checkout. The original investigation uses application source at `ae4eb5c`; those application files are unchanged through `cf694a9`. The patch changes four public fields and adds no adapter or parallel production implementation.

For the publication executable, add an investigation-only target to that checkout's `benchmarks/CMakeLists.txt`:

```cmake
add_executable(detentd_publication_bench container-alternative/publication_bench.cc)
target_link_libraries(detentd_publication_bench PRIVATE detentd_lib)
```

Build both arms with identical dependencies, compiler, SDK and Release options. `publication_pair.py BEFORE_BINARY AFTER_BINARY OUTPUT_DIRECTORY` runs five alternating pairs for actual PUT/PATCH publication, with file persistence, three measured updates and three overrides per knob. Wrap it in a strict `quiet run`. It uses macOS `/usr/bin/time -l` for whole-process peak RSS and the existing process-interference guard. Its output directory must not exist.

The in-process counters cover main-thread ordinary `operator new` calls during measured publication. Peak RSS covers the entire process, including initial seeding and destruction; it is not retained map memory. These are successful-operation probes, not fault-injection, fsync durability or network/fleet qualification. Baseline btree lacks the proposed move/copy corrections, so no result establishes superiority over a future corrected btree.

The October 22 extension of `model_footprint.cc` adds process CPU and deterministic shuffled insertion alongside ordered insertion. Its four rows distinguish short/maximum-length keys and ordered/random schedules. The original prototype report remains historical evidence; comparisons against repaired btree must rebuild both arms with this same extended source. CPU includes model construction, validation and destruction, and allocation counters include string heaps. It does not measure publication, RSS or durability.

The [October 22 comparison](../model-container-comparison-20261022.json) refreshes the baseline to ownership-qualified btree `25f9494`. It includes accepted five-pair actual-model construction, transfer, pruning and actual `Client`/`ClientReadView` measurements. The report embeds the exact measured probe sources; later runtime-count extensions do not retroactively identify those binaries. Rejected publication timing batches remain excluded.

`model_lookup.cc` accepts `--knobs N`, `--long` and `--hot`; counts must be positive and even. `model_footprint.cc` accepts `--knobs N`, `--key-profile short|long|both`, and optionally `--move` or `--move-pruned`. Defaults retain the original 100K workload. CPU includes validation; lookup timing excludes model setup, while model CPU includes construction, validation and destruction. The short-only profile permits controlled high-cardinality trials without silently allocating the maximum-length-name corpus too.

`unordered-map.patch` applies to an isolated original-model checkout, independently of `standard-map.patch`. It changes the large snapshot index to an unordered map and the three smaller model tables to standard ordered maps. It adds one sorted pointer traversal for JSON output, shared-memory output and authority import. The current diff already uses lookups and requires no ordered-merge adaptation. This is a measurement prototype, not the selected public API; its standard string hasher is not qualified against adversarial collisions.

The permanent `test_snapshot_canonical` can write JSON and shared-memory artifacts when passed an output directory. Build it in each candidate checkout and compare both files with `cmp`. Ascending, descending and shuffled insertion orders must all match. Apply the explicit reserved-byte fix from `24c10b5` to each arm: the previous shared-memory writer could expose unnamed record padding, independently of container ordering.

The [capacity plan](../../docs/CAPACITY_QUALIFICATION.md) defines the full-system matrix through 1.3M and exploratory 5M/10M tiers. Container CPU and allocation counts alone do not qualify ingestion, durable publication, mapped-agent RSS, recovery or fleet capacity.

The separate [capacity model report](../model-capacity-20261022.json) extends the same probes to the primary 1.3M point and exploratory 5M/10M tiers. Its short-name model retains exactly three overrides per boolean knob; it is not the mixed enterprise corpus. Ascending numeric IDs are not lexicographically sorted variable-width keys. Whole-process exploratory RSS covers both insertion schedules and includes allocation instrumentation, so it must not be presented as decoded agent memory.

`local_corpus.hh` derives one decimal key width from the runtime count and shares it with the paged lookup oracle. Existing six-digit fixtures retain their exact bytes. The `test_scale_corpus` fixture checks million-boundary ordering and native decoder record/string metadata without allocating a million-record unit-test fixture.
