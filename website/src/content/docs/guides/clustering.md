---
title: "Clustering"
---

A single distributor is a single point of failure for *updates*. Clients keep
serving their last known good snapshot when it's down, but nobody can publish.
Run three distributors as a Raft cluster and any one of them can fail.

---

## Start Three Replicas

On one host, for a demo, give each node its own port and state file:

```sh
mkdir -p .scratch
detentd 8081 .scratch/cluster-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 \
  127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 &
detentd 8082 .scratch/cluster-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 \
  127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083 &
detentd 8083 .scratch/cluster-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 \
  127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082 &
```

The `--cluster` arguments are, in order:

1. The cluster token, shared by every node (`demo`).
2. The UDP multicast port (`23864`).
3. The multicast group (`239.255.0.1`).
4. The multicast interface address (`127.0.0.1`).
5. This node's own advertised `host:port`.
6. Every peer's advertised `host:port`.

---

## Check the Leader

```sh
curl -s localhost:8081/v1/health
curl -s localhost:8082/v1/health
curl -s localhost:8083/v1/health
```

In clustered mode, health adds `clustered`, `is_leader` and `leader_address`.
Once the election converges, exactly one node reports `"is_leader": true`.

---

## Publish Through Any Node

Writes can go to any node. A follower forwards the command to the leader, and
the write is acknowledged once a quorum has committed it:

```sh
curl -s -D - -X PUT localhost:8082/v1/snapshot -d '{
  "schema_version": 1,
  "version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 }
  }
}'
```

The response is still `{"accepted_version":1}`, plus an `X-Detent-Leader`
header naming the current leader. Read it back from another node:

```sh
curl -s localhost:8083/v1/snapshot
```

Every node applies the same committed log in the same order, so every node
serves the same snapshot, the same ETag and the same diffs.

---

## Point Clients at All Nodes

Reads don't need the leader. Give the client every node:

```cpp
opts.source_poll = detent_http::make_http_poll_source({
    {"node-a", 8081}, {"node-b", 8081}, {"node-c", 8081},
});
```

Each client starts on a random node, so a fleet spreads its polls across the
cluster, and sticks to that node until a request fails. Then it moves on to the
next one.

---

## Failure Behavior

| Event | What happens |
| --- | --- |
| A follower dies | The surviving majority keeps committing writes; clients on that node fail over |
| The leader dies | The survivors elect a new leader; writes resume after the election |
| A majority is lost | Writes fail; every node still serves reads of the last committed snapshot |
| A node restarts | It sits out of voting for a safety window, then catches up and rejoins |

:::hint[Legacy clustering is for trusted networks]{.warning}
This Raft profile keeps its terms and votes in memory and talks over UDP
multicast with no authentication or encryption. It protects against a
restarted node voting twice with a timed silence window rather than durable
votes. Run it only on an access-controlled network. For durable consensus,
authenticated peers and TLS, use the
[durable authority](/Detent/guides/durable-authority/).
:::

See [Operations: Clustering](/Detent/operations/clustering/) for timing,
production layout and multi-region notes, and
[Raft Internals](/Detent/architecture/raft/) for how it works.
