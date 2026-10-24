---
title: "Read Path"
---

The read path is the code that runs every time your application calls `eval`
or `get`. Its contract is strict: no network call, file read, mutex, sleep,
retry, logging I/O or heap allocation in steady state.

---

## Step by Step

```d2 alt="eval and get validate the name, check the CLI override table, load the snapshot pointer atomically, look the knob up in a B-tree, then type-check and evaluate, hashing only when a rollout applies"
direction: down
app: "client.eval() / client.get()"
validate: "validate name\n(character table, no allocation)"
override: "CLI override lookup\n(one null check when unset)"
overrides: "OverrideTable\n(immutable after init)" {shape: cylinder}
load: "SnapshotRef::load()\n(one atomic acquire)"
snap: "current Snapshot\n(immutable)" {shape: cylinder}
lookup: "B-tree lookup\n(heterogeneous, string_view key)"
eval: "type check + precedence\n(group > rollout > default)"
hash: "stable_bucket()\n(SHA-256)"
app -> validate -> override -> load -> lookup -> eval
override -> overrides: read
load -> snap: read
eval -> hash: "only if a rollout applies\nand hash_val is present"
```

1. **Validate the name** against the knob-name character rules with a constant
   lookup table. Invalid names fail before touching any state.
2. **Check CLI overrides.** The override table is built once at startup and
   never changes. When there are no overrides, the pointer is null and this
   step is one branch.
3. **Load the snapshot.** `SnapshotRef` is an atomic shared pointer; loading it
   is one acquire operation plus a reference-count increment that keeps the
   snapshot alive for the duration of the read.
4. **Look up the knob** in an immutable sorted `detent::flat_map` with transparent
   comparison, so the `std::string_view` name is used directly without
   building a `std::string`.
5. **Evaluate** with the [precedence rules](/Detent/reference/evaluation/).
   Only a rollout with a hash value pays for SHA-256.
6. **Count** the outcome in a relaxed atomic counter and return.

---

## Why an ordered map

The knob table is a sorted vector, `detent::flat_map`, rather than a tree or
a hash table. Lookups binary-search one contiguous array, ordered iteration
makes serializing, diffing and merging snapshots linear passes, and entry
moves never throw, so a refused allocation leaves a table unchanged.

---

## Why Immutable Snapshots

Each update builds a brand-new `Snapshot` and publishes it with a single
release store. Readers that loaded the old pointer keep using the old snapshot
until their read finishes; the shared-pointer reference count frees it after
the last reader lets go. There's no reader lock, no writer lock on the read
side, and no possibility of seeing half an update.

---

## Pinned Views

`read_view()` performs the load once and hands you a `ClientReadView` that
holds the snapshot. Every read through the view sees the same configuration,
and the view stays valid across updates and even after the client is
destroyed.

For managed generations, the view instead holds an immutable evaluator over a
memory-mapped image, published by the source's refresh worker. Acquiring it is
again an atomic load, with no filesystem work and no allocation.

---

## Measured Cost

| Benchmark | Result |
| --- | --- |
| `detent_read_bench 4 2000000` | 6.71M reads/s across 4 threads, 149.1 ns/read |
| `detent_hash_bench 5000000` | 198.2 ns per `stable_bucket()` call |

The concurrency stress test runs readers against a continuously swapping
updater, and the suite runs clean under ASan, UBSan and TSan on Linux.
