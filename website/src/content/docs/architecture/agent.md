---
title: "Agent"
---

`detent-agent` exists so that a host needs one connection to the authority or
a [relay](/Detent/architecture/relay/), not one per process, and so that
processes that can't link C++ still get fast, local reads.

The agent started with `--managed managed.json` receives snapshots and deltas
upstream and publishes immutable generations, described in
[Delivery and Local Images](/Detent/architecture/delivery/).

| | |
| --- | --- |
| Upstream | Authority or relay, TLS, bearer token |
| Local state | `generation_dir` (`selected` plus immutable generations) |
| Build | `DETENT_BUILD_MANAGED_AGENT=ON` (default) |
| First start | `--initialize-managed` once on an empty directory |

---

## Design

```d2 alt="detent-agent receives snapshots and deltas, builds an immutable generation, selects it atomically, and readers map the selected generation read-only"
direction: down
source: "authority or relay"
agent: "detent-agent"
gen: "generation_dir\n(selected + immutable generations)" {shape: cylinder}
r1: "detent get"
r2: "other local readers"
source -> agent: "snapshot, delta"
agent -> gen: "build, select"
gen -> r1: "lease + mmap PROT_READ"
gen -> r2: "lease + mmap PROT_READ"
```

Each update is written to a new, dynamically sized image file, validated, and
then selected atomically through a small descriptor. Readers pin a generation
through a lease file and read bytes nobody will ever modify; they evaluate
directly against the mapping, using the same `stable_bucket()` function as the
client.

---

## Lifecycle

1. `detent get` starts a user agent on demand when a managed configuration is
   present. System agents are started only by the service manager.
2. The agent holds a lock on `agent.pid`; `--status` reads it without starting
   anything.
3. SIGTERM drains logs and exits cleanly.
4. If the upstream disappears, the agent keeps the last selected generation
   and keeps retrying.
