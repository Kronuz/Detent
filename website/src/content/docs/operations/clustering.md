---
title: "Clustering"
---

This page covers running the classic distributor as a Raft cluster in
production. For a first walkthrough, see the [Clustering guide](/Detent/guides/clustering/).

---

## Topology

Run an odd number of nodes, three or five, on separate hosts. A cluster of `2f
+ 1` nodes keeps accepting writes with `f` nodes down. Reads keep working on
every surviving node regardless, because every node serves the last committed
snapshot.

```sh
# On 10.0.0.1 (repeat on .2 and .3 with their own SELF address)
detentd --port 8317 --persist-path /var/lib/detent/snapshot.json \
  --cluster prod-knobs 23864 239.255.0.1 10.0.0.1 \
  10.0.0.1:8317 10.0.0.2:8317 10.0.0.3:8317
```

1. Every node lists the same peers; each puts its own address first.
2. Peers are addressed by the configured `host:port` list, not by the multicast
   sender address.
3. The multicast group must be routable between the nodes, which usually means
   one network segment.
4. Give every environment a distinct cluster token, such as `knobs-dev`,
   `knobs-staging` and `knobs-prod`, so nodes never join the wrong cluster.

---

## Timing

```text
--raft-timing HEARTBEAT ELECTION_INIT ELECTION_MIN ELECTION_MAX
```

The defaults are `1.0 4.0 10.0 30.0` seconds, tuned for infrequent
configuration writes rather than fast failover. Every node must use the same
values.

---

## Restarts

The classic Raft profile keeps its term and vote in memory. To keep a restarted
node from voting twice in a term it already voted in, it stays silent, neither
voting nor standing for election, for `3 × election_max` after a restart. A
`<persist_path>.cluster_marker` file tells a genuine first launch, which skips
the window, from a restart.

With default timing that window is 90 seconds. Restart nodes one at a time and
wait for each to rejoin before restarting the next.

---

## Failure Modes

| Event | Effect | Recovery |
| --- | --- | --- |
| Follower down | None for clients that fail over | Restart; it catches up |
| Leader down | Writes pause until a new election | Automatic |
| Majority down | Writes return `503`; reads continue | Restore a majority |
| Permanently lost node | The majority keeps committing | Replace it; membership is static |

A permanently dead peer doesn't block replication: liveness is tracked from the
last message received from each peer.

---

## Multi-Region

:::hint[Multi-region replication]{.unimplemented}
One cluster spans one region. The planned design runs one Raft cluster per
region with one primary: each secondary region's leader polls the primary with
the same diff-aware polling clients use, and resubmits updates through its own
consensus. Cross-region propagation would be asynchronous and add no latency to
local writes. This isn't implemented yet.
:::

---

## Limits of the Classic Profile

1. No authentication or encryption on HTTP or multicast.
2. Volatile Raft state, compensated by the restart silence window.
3. Static membership.
4. The full snapshot is copied and persisted on every write.

The [durable authority](/Detent/operations/authority/) addresses all four.
