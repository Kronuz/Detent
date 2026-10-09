---
title: "Delivery and Local Images"
---

Managed delivery moves committed authority state to hosts and makes it
readable without any per-read I/O. It replaces both the JSON poll and the
legacy agent's mutable shared-memory file.

---

## End to End

```d2 alt="The authority captures a frozen scoped projection after a quorum read and encodes a snapshot, delta or progress record into framed MessagePack; the managed agent validates it against its pinned base, builds a new immutable image, publishes it with a descriptor swap, and readers lease and map the selected generation"
direction: down
authority: "Authority\n(quorum read, frozen scoped capture)"
encode: "Encoder\n(snapshot | exact-base delta | progress)"
frames: "Bounded frames\n(raw or zstd fragments)"
agent: "Managed agent\n(validate against pinned base)"
build: "Builder\n(new immutable image)"
select: "Atomic descriptor swap\n(DTSELECT)"
readers: "Readers\n(lease + mmap)"
authority -> encode -> frames -> agent -> build -> select -> readers
```

---

## Scope and Cursors

A subscription is identified by a **scope identity**: SHA-256 over a fixed
domain string, the stable principal, the policy epoch and the sorted list of
authorized namespaces. Tokens never enter it, so rotating a token preserves the
scope; changing grants requires a new policy epoch.

A **cursor** names exactly where a host is: authority cluster, group, epoch,
scope and revision. The authority sends a delta only if its retained,
changed-only history exactly covers the requested base cursor; anything else
gets a full snapshot. A mismatched base is never patched.

---

## The Codec

Delivery uses a narrow, typed MessagePack schema with its own bounded parser
rather than a general MessagePack library. In the 500,000-knob comparison it
measured 123–130 ms to encode and 621–627 ms to decode. JSON with zstd produced
about half the compressed bytes for more CPU; Protobuf Lite used more CPU in
the same corpus. Every structural, semantic and decompression limit is
checked before allocation.

---

## Immutable Generations

Each generation is a single image file:

1. A 192-byte header binding the complete cursor and semantic profile.
2. 24-byte namespace rows with revision watermarks.
3. 48-byte record rows with namespace index, key revision and definition.
4. 24-byte override rows.
5. One dense string region.

All integers are big-endian; every region, ordering and string is validated
before a reader is exposed to it. A separate 176-byte `DTSELECT` descriptor,
named `selected`, records the active generation's identity, length, SHA-256
checksum, cursor and profile. Files are named only from the generation's hex
identity, so knob names never become paths.

Publication writes and syncs a complete new image, then atomically replaces the
descriptor. Readers:

1. Open the directory and descriptor without following symlinks, and check
   ownership, link counts and permissions.
2. Take a shared, non-blocking lock on the generation's lease file.
3. Verify length and checksum, validate every row, and map the image.

Cleanup must take an exclusive lock on the same lease before deleting a retired
image, so a pinned generation is never removed under a reader. Nobody ever
modifies a sealed image, which removes the legacy seqlock's data race
entirely.

---

## Resource Accounting

The managed agent charges every byte it holds against explicit ledgers: heap,
mapped address space, named disk bytes, disk entries and generations. Disk
lifecycle is tracked with durable ownership records, a streaming census at
startup, and batched, lease-protected garbage collection. Failure to publish
keeps the previous selection, and recovery after a crash re-derives the exact
state from disk instead of guessing.

---

## Status

Managed delivery and the SDK source have component acceptance. Resumable
subscriptions and snapshot/history handoff are in progress; regional relays and
incremental page-level publication come next. See the
[Roadmap](/Detent/roadmap/).
