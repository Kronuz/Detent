# Sorted-vector flat_map for the four model tables

Measured on `explore/flat-map` at `3ec0df4` (base `1766ef0`); the model and merge code is unchanged on `flat-on-tip`. Raw medians, commands, binary hashes and limits are in
`benchmarks/flat-map-exploration-20261023.json`. macOS arm64, Release, 100,000 knobs, five rounds,
three arms (std::map, flat, btree), medians; flat/std ratios below, spread in the JSON.

## Result

One experiment, a win on memory and most CPU, one pathology.

| | short ordered | short random | long ordered | long random |
|---|---|---|---|---|
| live snapshot requested bytes, flat / std::map | 0.58 | 0.58 | 0.86 | 0.86 |
| flat / btree arm | 0.85 | 0.77 | 0.96 | 0.94 |
| parse cpu, flat / std::map | 0.65 | 0.86 | 0.81 | 0.93 |
| copy cpu | 0.09 | 0.12 | 0.34 | 0.56 |
| apply_patch cpu | 0.16 | 0.15 | 0.41 | 0.70 |
| diff_snapshots cpu | 0.07 | 0.06 | 0.10 | 0.28 |
| broad find cpu | 0.44 | 0.87 | 0.63 | 0.97 |
| allocations in parse | 0.23 | 0.23 | 0.31 | 0.31 |

Lookup rows (detent_model_lookup): hits within 0.82-1.05 of std::map, rollout rows up to 1.05,
misses 0.62-0.95. `cleanup_turn_ns` (erase of the last override at 256 overrides): p99 42 ns,
max 84-1084 ns, zero allocations; erase of `begin()` on the same table: p99 167 ns, max 416-8750 ns.

Costs: `prune_half_positional` (erase every other element by iterator, 50,000 erases) takes about
7.5 s against 2.7-5.5 ms: positional erase on a 100,000-entry table is O(n) per call. The bench now
prunes with `erase_if`, one linear pass. Production paths build tables with `flat_map_builder`
(JSON snapshots, patches, overrides and CLI overrides), merge them linearly, or drain from the
back; any future bulk update must do the same.

## Identity

ctest 211/211, canonical bytes identical to the std::map arm, lifecycle digests identical in all
four configurations on all three arms, and the duplicate-precedence test passes on both the
std::map arm and this one.
