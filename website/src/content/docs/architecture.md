---
title: "Architecture Overview"
---

Detent separates three jobs that most configuration systems blur together:
**deciding** what the configuration is, **distributing** it, and **serving**
it to code. Each runs at a different speed and fails differently, and keeping
them apart is what lets a read stay a few hundred nanoseconds while
distribution can be down for an hour.

---

## The Big Picture

```d2 alt="Publishers write to detentd or the authority; clients and agents poll or receive deliveries; applications and shell readers read local snapshots"
direction: down
publisher: Publishers {shape: person}
control: "Control plane" {
  detentd: "detentd\n(classic, optional Raft)"
  authority: "detentd --authority\n(durable Raft, TLS)"
}
host: "Each host" {
  client: "App process\n(detent::Client)"
  agent: "detent-agent"
  shm: "shared memory /\nimmutable generations" {shape: cylinder}
  shell: "detent get / any language"
}
publisher -> control.detentd: "PUT / PATCH /v1/snapshot"
publisher -> control.authority: "POST /v2/transactions"
control.detentd -> host.client: "GET /v1/snapshot\n(ETag + diffs)"
control.detentd -> host.agent: "GET /v1/snapshot"
control.authority -> host.agent: "/v2/distribution\n(snapshot or delta)"
host.agent -> host.shm: publish
host.shell -> host.shm: "mmap, read-only"
```

---

## Three Planes

### Data Plane: Reads

The read path runs inside your process and does nothing but memory access:
validate the name, check the CLI override table, load the current snapshot
pointer, look up the knob, and evaluate it. No network, disk, lock or
allocation. See [Read Path](/Detent/architecture/read-path/).

### Update Plane: the Client's Background Thread

One background thread per client fetches candidates from its source, validates
them completely, gates them by version, merges them over the baked defaults,
and publishes the result with one atomic store. Failures are retried with
jittered backoff and never reach readers. See
[Update Path](/Detent/architecture/update-path/).

### Control Plane: Distribution

`detentd` accepts publishes and serves snapshots. In its classic profile it is
a small HTTP server with persistence, cached diffs and compression, optionally
replicated with Raft. In its authority profile it is a durable, authenticated,
transactional state machine with completion-based storage I/O. See
[Distributor](/Detent/architecture/distributor/),
[Raft](/Detent/architecture/raft/) and [Authority](/Detent/architecture/authority/).

---

## Design Principles

1. **Reads never wait on distribution.** A distribution failure degrades
   freshness, never availability.
2. **Immutable snapshots, atomic swaps.** Readers see one complete,
   validated configuration or the previous one, never a mixture.
3. **Fail fast on programmer errors.** A missing knob is a bug, so it's loud;
   a missing distributor is an outage, so it's quiet and observable.
4. **Whole-definition replacement.** A knob is an atomic unit; there's no
   field-level merge to reason about.
5. **Everything is bounded.** Request bodies, snapshot sizes, queues, history,
   retained memory and disk are all limited before allocation.
6. **Measured, not asserted.** Performance claims come with a reproducible
   benchmark and its caveats.

---

## Where to Read More

1. [Read Path](/Detent/architecture/read-path/) and
   [Update Path](/Detent/architecture/update-path/): the client.
2. [Distributor](/Detent/architecture/distributor/): the classic server.
3. [Agent and Shared Memory](/Detent/architecture/agent/): host-local reads.
4. [Raft Clustering](/Detent/architecture/raft/): the classic replication.
5. [Durable Authority](/Detent/architecture/authority/): transactions and
   durable consensus.
6. [Delivery and Local Images](/Detent/architecture/delivery/): managed
   distribution and immutable generations.
7. [Dependencies](/Detent/architecture/dependencies/): the libraries Detent is
   built from.
8. [Design Documents](/Detent/architecture/design-documents/): the full
   engineering record in the repository.
