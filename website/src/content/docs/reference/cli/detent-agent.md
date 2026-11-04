---
title: "detent-agent"
---

```text
detent-agent --managed PRIVATE_CONFIG [--daemonize] [--generation-dir PATH]
             [--initialize-managed | --upgrade-managed]
detent-agent --status
detent-agent --help | -h
```

The host-local agent. It receives authenticated deliveries from the durable
authority (or a relay) and publishes immutable generations on disk. Without
`--managed` (or `managed_path` in `agent.json`) it exits with status 1 and
`no source configured`.

---

## Managed Options

| Option | Meaning |
| --- | --- |
| `--managed PRIVATE_CONFIG` | The private credential file, `managed.json` |
| `--generation-dir PATH` | Durable generation directory |
| `--initialize-managed` | Provision an empty generation directory (first start only) |
| `--upgrade-managed` | Adopt a compatible older generation directory |
| `--generation-cluster HEX32`, `--generation-group HEX32`, `--generation-epoch HEX32`, `--generation-scope HEX64` | Expected public identity; all four or none |

See [Managed Delivery](/Detent/guides/managed-delivery/) for `managed.json`.

## Common Options

| Option | Meaning |
| --- | --- |
| `--config PATH` | Use this `agent.json` |
| `--runtime-dir PATH` | Directory for `agent.pid` and `agent.log` |
| `--system` | Use system configuration and runtime locations |
| `--daemonize` | Run in the background (user operation only) |
| `--status` | Report the running agent and its durable generation, then exit |

`--system --daemonize` and `--status --daemonize` are rejected.

---

## Status

```sh
detent-agent --status --managed managed.json
# running (pid 65214)
# durable revision=3 knob_count=2
```

With a generation directory (`--generation-dir PATH`, or implied by
`--managed`), `--status` also reports the durable revision and count
independently of whether the process is running. The exit status is 0 when the
agent is running and 1 otherwise.

---

## Environment

| Variable | Meaning |
| --- | --- |
| `DETENT_MANAGED_CONFIG` | Private managed configuration |
| `DETENT_GENERATION_DIR` | Durable generation directory |
| `DETENT_RUNTIME_DIR` | Runtime directory |

---

## Files

| File | Location | Purpose |
| --- | --- | --- |
| `agent.json` | Config directory | Optional configuration |
| `agent.pid` | Runtime directory | Lock and PID |
| `agent.log` | Runtime directory | Background (`--daemonize`) logs |

The runtime directory defaults to `$XDG_RUNTIME_DIR/detent`, else a private
`detent-<uid>` directory under `$TMPDIR` (mode `0700`); system agents use
`/run/detent` on Linux and `/var/run/detent` on BSD and macOS.
