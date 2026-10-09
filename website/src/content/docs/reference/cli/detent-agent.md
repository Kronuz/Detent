---
title: "detent-agent"
---

```text
detent-agent [--daemonize] [--defaults PATH] [--source-path PATH]
             [--source-address HOST:PORT] [--poll-interval-ms N]
detent-agent --managed PRIVATE_CONFIG [--generation-dir PATH]
             [--initialize-managed | --upgrade-managed]
detent-agent --status
detent-agent --help | -h
```

The host-local agent. In **legacy** mode it runs one `detent::Client` against a
distributor or file and republishes every accepted snapshot into shared memory.
In **managed** mode it receives authenticated deliveries from the durable
authority and publishes immutable generations on disk.

---

## Legacy Mode Options

| Option | Meaning |
| --- | --- |
| `--source-address HOST:PORT` | Poll this distributor's `/v1/snapshot` (alias: `--source-http`) |
| `--source-path PATH` | Poll this snapshot file instead |
| `--defaults PATH` | Typed fallback knobs |
| `--poll-interval-ms N` | Poll interval; default `200` |

With neither source nor defaults, the agent polls `127.0.0.1:8317`. With only
defaults, it serves them without any network connection. A source flag replaces
the configured source as a whole.

## Managed Mode Options

| Option | Meaning |
| --- | --- |
| `--managed PRIVATE_CONFIG` | The private credential file, `managed.json` |
| `--generation-dir PATH` | Durable generation directory |
| `--initialize-managed` | Provision an empty generation directory (first start only) |
| `--upgrade-managed` | Adopt a compatible legacy generation directory |
| `--generation-cluster HEX32`, `--generation-group HEX32`, `--generation-epoch HEX32`, `--generation-scope HEX64` | Expected public identity; all four or none |

See [Managed Delivery](/Detent/guides/managed-delivery/) for `managed.json`.

## Common Options

| Option | Meaning |
| --- | --- |
| `--config PATH` | Use this `agent.json` |
| `--runtime-dir PATH` | Directory for `agent.pid`, `agent.log` and `snapshot.shm` |
| `--system` | Use system configuration and runtime locations |
| `--daemonize` | Run in the background (user operation only) |
| `--status` | Report the running agent and its snapshot, then exit |

`--system --daemonize` and `--status --daemonize` are rejected.

---

## Status

```sh
detent-agent --status
# running (pid 65214)
# OK version=3 knob_count=2
```

For a managed directory, `--status --generation-dir PATH` reports the durable
revision and count independently of whether the process is running.

---

## Environment

| Variable | Meaning |
| --- | --- |
| `DETENT_SOURCE_ADDRESS` | `host:port` of the distributor |
| `DETENT_SOURCE_HTTP` | Legacy alias for `DETENT_SOURCE_ADDRESS` |
| `DETENT_SOURCE_PATH` | Snapshot file path |
| `DETENT_DEFAULTS_PATH` | Defaults file |
| `DETENT_POLL_INTERVAL_MS` | Poll interval |
| `DETENT_RUNTIME_DIR` | Runtime directory |

---

## Files

| File | Location | Purpose |
| --- | --- | --- |
| `agent.json` | Config directory | Optional configuration |
| `defaults.json` | Beside `agent.json` | Optional local fallback knobs, discovered automatically |
| `agent.pid` | Runtime directory | Lock and PID |
| `agent.log` | Runtime directory | Background (`--daemonize`) logs |
| `snapshot.shm` | Runtime directory | The published shared-memory snapshot |

The runtime directory defaults to `$XDG_RUNTIME_DIR/detent`, else a private
`detent-<uid>` directory under `$TMPDIR` (mode `0700`); system agents use
`/run/detent` on Linux and `/var/run/detent` on BSD and macOS.
