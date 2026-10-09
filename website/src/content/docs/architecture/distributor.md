---
title: "Distributor"
---

The classic `detentd` is deliberately small: three handlers over one shared,
immutable current snapshot, plus persistence, a diff cache and a compression
cache.

---

## Inside One Node

```d2 alt="PUT and PATCH validate and version-gate, merge under the snapshot lock, persist atomically, and precompute ETag, diffs and compressed bodies; GET serves cached bytes under a shared lock"
direction: down
put: "PUT / PATCH /v1/snapshot\n(validate + version gate)"
commit: "resolve_and_commit()\n(merge under exclusive lock)"
state: "current snapshot\n+ ETag + version" {shape: cylinder}
caches: "precomputed:\nfull body, step diff,\nzstd + gzip variants" {shape: cylinder}
history: "diff history\n(last 64 steps)" {shape: queue}
disk: "persisted snapshot\n(write temp, rename)" {shape: page}
get: "GET /v1/snapshot\n(conditional, instant)"
put -> commit
commit -> disk: "durable first"
commit -> state
commit -> caches
commit -> history
get -> state: "shared lock"
get -> caches
get -> history: "compose diff\nfor older bases"
disk -> state: "reload at startup"
```

---

## Publishing

`PUT` and `PATCH` both go through `resolve_and_commit()`, the same function
whether the node is standalone or applying a committed Raft entry:

1. Parse and validate the body completely.
2. Take the snapshot lock **exclusively**, then check the version and, for a
   patch, merge onto the locked current state. Merging outside the lock would
   let two concurrent patches merge onto the same base and the second commit
   would silently drop the first; the version check alone can't catch that.
3. Persist to disk with write-then-rename.
4. Swap in the new snapshot and recompute its ETag.
5. Precompute the step diff from the previous version and the compressed
   variants of both the full body and the diff.

All the per-update work happens once, at publish time.

---

## Serving

`GET /v1/snapshot` never blocks and never computes anything expensive:

1. `If-None-Match` matching the weak ETag returns `304`.
2. `X-Detent-Base-Version` equal to the previous version returns the cached
   step diff.
3. An older base within the 64-step history gets a diff composed from the
   retained steps. Composed diffs are cached by base version.
4. Anything else returns the cached full body.

Compression is chosen from the cached variants by `Accept-Encoding`, preferring
zstd.

### Why Polling Instead of Long-Polling

The HTTP framework runs handlers synchronously to completion, so a long-poll
would park a worker thread per waiting client. Short conditional `GET`s keep
capacity bounded by request rate rather than client count; an unchanged poll
costs a `304` with no body. The trade-off is up to one poll interval of
propagation delay, inside Detent's eventually-consistent contract.

---

## Measured Behavior

On a single machine with a 200 ms poll interval, 1,000 pollers converge with
p99 around 200–215 ms; 2,000 is where the single-machine harness stops giving
clean answers. See [Benchmarks](/Detent/benchmarks/#distribution-fan-out).

---

## Limits of the Design

Every write copies the whole map, rewrites the whole persisted file, and
derives its diff by comparing complete states. At 500,000 knobs a one-knob
patch costs about 780 ms of CPU. That's fine for typical configurations and
infrequent writes, and it's exactly what the
[durable authority](/Detent/architecture/authority/) replaces.
