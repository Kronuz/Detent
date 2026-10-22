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
