---
title: "Agent"
---

`detent-agent` exists so that a host needs one connection to the authority, not
one per process, and so that processes that can't link C++ still get fast,
local reads.

With `--managed managed.json` the agent receives snapshots and deltas from the
authority or a [relay](/Detent/architecture/relay/) over verified TLS and
publishes immutable generations, described in
[Delivery and Local Images](/Detent/architecture/delivery/). `detent get` and
the C++ managed source read those generations. Without `--managed` (or
`managed_path` in `agent.json`) the agent exits with `no source configured`.

| | Managed agent |
| --- | --- |
| Upstream | Authority or relay, TLS, bearer token |
| Local state | `generation_dir` (`selected` plus immutable generations) |
| Build | `DETENT_BUILD_MANAGED_AGENT=ON` (default) |
| First start | `--initialize-managed` once on an empty directory |

---

## Design

```d2 alt="The managed agent receives deliveries from the authority and publishes immutable generations that readers map read-only"
direction: down
upstream: "authority or relay"
agent: "detent-agent\n(managed delivery)"
store: "generation_dir\n(immutable images + selected)" {shape: cylinder}
r1: "detent get"
r2: "other local readers"
upstream -> agent: "TLS delivery"
agent -> store: "build, validate, select"
store -> r1: "mmap read-only"
store -> r2: "mmap read-only"
```

Readers pin a generation through a lease file and read bytes nobody will ever
modify, so a reader never races a writer. See
[Delivery and Local Images](/Detent/architecture/delivery/).

---

## Lifecycle

1. `detent get` starts a user agent on demand and waits for its first
   generation. System agents are started only by the service manager.
2. The agent holds a lock on `agent.pid`; `--status` reads it without starting
   anything.
3. SIGTERM drains logs and exits cleanly.
4. If the upstream disappears, the agent keeps the last selected generation and
   keeps retrying.
