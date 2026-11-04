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

```d2 alt="Producers write transactions to the authority; the optional relay subscribes to it and serves snapshots and deltas to each host's agent, which publishes immutable generations that the app process and detent get map and read"
direction: down
producer: Producers {shape: person}
authority: "Authority\ndetentd --authority\n(3 voters, durable Raft)" {style.multiple: true}
relay: "Relay (optional)\ndetent-relay"
host: "Each host" {
  label.near: bottom-center
  agent: "detent-agent"
  gens: "immutable generations" {shape: cylinder}
  client: "App process\n(detent::Client)"
  shell: "detent get"
  agent -> gens: publish
  client -> gens: "map, read"
  shell -> gens: "map, read"
}
producer -> authority: "POST /v2/transactions"
authority -> relay: "subscription"
relay -> host.agent: "snapshot or delta"
authority -> host.agent: "or direct"
```

This is the managed profile, the topology Detent's production contract
targets.

---

## The Actors

| Actor | What it does | Runs on | Binary |
| --- | --- | --- | --- |
| Producer | Writes knob changes as namespaced transactions | Anywhere that reaches the authority | Your service, or `curl` |
| Authority | Orders, stores and serves the source of truth | Three machines, one voter each | `detentd --authority authority.json` |
| Relay | Optional fan-out: one upstream subscription per bundle, many agents | Between the authority and agent hosts | `detent-relay` |
| Agent | Receives snapshots and deltas, publishes immutable generations | Every application host | `detent-agent --managed managed.json` |
| Client | Maps the selected generation and reads in memory | Every application host | Your app (`detent::Client`), `detent get` |

Voters, relays and agents are all daemons; "daemon" names no role. Each actor
has its own page:
[Authority](/Detent/architecture/authority/),
[Relay](/Detent/architecture/relay/),
[Agent](/Detent/architecture/agent/) and
[Delivery and Local Images](/Detent/architecture/delivery/) for the agent's
generations, and [Read Path](/Detent/architecture/read-path/) for the client.
[Detent's actors](https://github.com/Kronuz/Detent/blob/main/docs/ACTORS.md) in
the repository is the full reference: endpoints, files, build options, default
directories and restart behavior for every actor.

---

## Three Planes

### Data Plane: Reads

The read path runs inside your process and does nothing but memory access:
validate the name, check the CLI override table, load the current snapshot
pointer, look up the knob, and evaluate it. No network, disk, lock or
allocation. See [Read Path](/Detent/architecture/read-path/).

### Update Plane: the Client's Background Thread

One background thread per client picks up new state from its source. With
managed delivery, it watches the agent's `selected` descriptor and maps each
new generation. With a file or HTTP source, it fetches candidates, validates them
completely, gates them by version, merges them over the baked defaults, and
publishes the result with one atomic store. Failures never reach readers. See
[Update Path](/Detent/architecture/update-path/).

### Control Plane: Distribution

The authority accepts transactions and replicates them durably across its
voters; relays and agents carry the committed state to every host as
snapshots and deltas. See [Authority](/Detent/architecture/authority/) and
[Relay](/Detent/architecture/relay/).

---

## Design Principles

1. **Reads never wait on distribution.** A distribution failure degrades
   freshness, never availability.
2. **Immutable snapshots, atomic swaps.** Readers see one complete,
   validated configuration or the previous one, never a mixture.
3. **Fail fast on programmer errors.** A missing knob is a bug, so it's loud;
   an unreachable source is an outage, so it's quiet and observable.
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
2. [Agent and Shared Memory](/Detent/architecture/agent/): host-local reads.
3. [Durable Authority](/Detent/architecture/authority/): transactions and
   durable consensus.
4. [Relay](/Detent/architecture/relay/): optional durable fan-out.
5. [Delivery and Local Images](/Detent/architecture/delivery/): managed
   distribution and immutable generations.
6. [Dependencies](/Detent/architecture/dependencies/): the libraries Detent is
   built from.
7. [Design Documents](/Detent/architecture/design-documents/): the full
   engineering record in the repository.
