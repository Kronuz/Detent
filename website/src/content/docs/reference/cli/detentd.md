---
title: "detentd"
---

```text
detentd [--daemonize | --status] [--runtime-dir PATH] [--config PATH]
        [--port N] [--persist-path PATH] [--reactors N] [--workers N]
        [--cluster TOKEN MCAST_PORT MCAST_GROUP INTERFACE SELF_HOST:PORT PEER_HOST:PORT ...]
        [--raft-timing HEARTBEAT ELECTION_INIT ELECTION_MIN ELECTION_MAX]
detentd [<port> <persist_path>] ...
detentd --authority CONFIG [--port N] [--daemonize | --status]
```

The distributor. Without arguments it listens on port `8317` and persists to
`snapshot.json` in the user state directory, creating it on first publish.

---

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--port N` | `8317` | HTTP port |
| `--persist-path PATH` | platform state dir | Where the accepted snapshot is persisted |
| `--reactors N` | `4` | Network reactor threads |
| `--workers N` | `8` | HTTP worker threads |
| `--config PATH` | platform config dir | Use this `detentd.json` |
| `--runtime-dir PATH` | platform runtime dir | Where pid and log files go |
| `--system` | | System config and state (`/var/lib/detent`, BSD/macOS `/var/db/detent`) |
| `--daemonize` | | Run in the background |
| `--status` | | Query the local instance's health and PID, then exit |
| `--authority CONFIG` | | Run the [durable authority](/Detent/guides/durable-authority/) instead |

The legacy positional form `detentd 8317 ./state/snapshot.json` is still
accepted. PID and log filenames include the port, so several instances can
share one runtime directory.

### Clustering

```text
--cluster TOKEN MCAST_PORT MCAST_GROUP INTERFACE SELF_HOST:PORT PEER_HOST:PORT ...
```

Joins a Raft cluster over UDP multicast. `TOKEN` names the cluster;
`INTERFACE` is the multicast interface address; `SELF_HOST:PORT` is this node's
advertised address, followed by every peer's. See
[Clustering](/Detent/guides/clustering/).

```text
--raft-timing HEARTBEAT ELECTION_INIT ELECTION_MIN ELECTION_MAX
```

Overrides Raft timing in seconds; the defaults are `1.0 4.0 10.0 30.0`. Every
node in a cluster should use the same values.

`--authority` can't be combined with `--cluster` or `--persist-path`.

---

## Environment

| Variable | Overrides |
| --- | --- |
| `DETENTD_PORT` | `port` |
| `DETENTD_PERSIST_PATH` | `persist_path` |
| `DETENTD_REACTORS` | `reactors` |
| `DETENTD_WORKERS` | `workers` |

---

## Configuration

`detentd.json` lives in the same directories as `agent.json`:

```json
{
  "port": 8317,
  "persist_path": "state/snapshot.json",
  "reactors": 4,
  "workers": 8,
  "runtime_dir": "/run/detent",
  "log_level": "info",
  "verbosity": 0,
  "color": "auto"
}
```

All fields are optional. Cluster membership and Raft timing are command-line
only.

---

## Status

```sh
detentd --status --port 8317
# running (pid 64308)
# {"has_snapshot":true,"current_version":2,"requests_served":7,...}
```

For an authority with publisher TLS, `detentd --status --authority CONFIG`
checks the HTTPS endpoint with CA and name verification.
