---
title: "Agent and Shared Memory"
---

`detent-agent` exists so that a host needs one poller, not one per process, and
so that processes that can't link C++ still get fast, local reads.

---

## Design

```d2 alt="detent-agent runs one Client whose on_update hook serializes each accepted snapshot into a seqlock-guarded shared-memory file that readers map read-only"
direction: down
source: "detentd or file"
agent: "detent-agent\n(one detent::Client)"
writer: "ShmWriter\n(on_update hook)"
shm: "snapshot.shm\n(flat binary, seqlock)" {shape: cylinder}
r1: "detent get"
r2: "other local readers"
source -> agent: poll
agent -> writer: "accepted snapshot"
writer -> shm: "seq++, memcpy, seq++"
shm -> r1: "mmap PROT_READ,\nbinary search"
shm -> r2: "mmap PROT_READ"
```

The agent reuses the ordinary client for polling, validation, merging and
backoff. Its only addition is the `on_update` hook, which serializes each
accepted snapshot into a flat, pointer-free layout: a header, sorted knob
records, override records and a string region. Readers binary-search the
records and evaluate directly against the mapped bytes, using the same
`stable_bucket()` function as the client.

---

## The Seqlock

```text
writer:  seq++ (odd) -> copy payload and header -> seq++ (even)
reader:  s1 = seq; if odd, retry
         read and evaluate
         s2 = seq; if s1 != s2, retry
```

The writer never waits for readers. A reader retries only if it actually
raced a write, which is rare when updates arrive every few hundred
milliseconds at most.

Two honest caveats:

1. **Bounds safety.** A reader can observe torn bytes mid-write, not just a
   torn result. Every offset, length and count it reads is bounds-checked
   against the payload length before it's dereferenced; anything that doesn't
   fit is treated as absent, and the seqlock retry sorts out whether it was a
   race.
2. **Formal data races.** The writer's plain `memcpy` and the readers' plain
   loads race under the ISO C++ memory model, the same gap the Linux kernel's
   seqlocks have in C. It's correct on the targeted platforms and covered by a
   dedicated stress test, but it isn't formally race-free.

---

## Its Successor: Immutable Generations

The legacy layout has a fixed 4 MiB capacity and the formal race above. The
managed agent replaces it with **immutable generations**: each update is
written to a new, dynamically sized image file, validated, and then selected
atomically through a small descriptor. Readers pin a generation through a lease
file and read bytes nobody will ever modify. See
[Delivery and Local Images](/Detent/architecture/delivery/).

---

## Lifecycle

1. `detent get` starts a user agent on demand and waits for its first
   snapshot. System agents are started only by the service manager.
2. The agent holds a lock on `agent.pid`; `--status` reads it without starting
   anything.
3. SIGTERM drains logs and exits cleanly.
4. If the source disappears, the agent keeps serving the last published
   snapshot and keeps retrying.
