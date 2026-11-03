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
authority or a relay and publishes immutable generations on disk.

---

## Options

| Option | Meaning |
| --- | --- |
| `--managed PRIVATE_CONFIG` | The private credential file, `managed.json` |
| `--generation-dir PATH` | Durable generation directory |
| `--defaults PATH` | Typed fallback knobs |
| `--initialize-managed` | Provision an empty generation directory (first start only) |
| `--upgrade-managed` | Adopt a compatible legacy generation directory |
| `--generation-cluster HEX32`, `--generation-group HEX32`, `--generation-epoch HEX32`, `--generation-scope HEX64` | Expected public identity; all four or none |

See [Managed Delivery](/Detent/guides/managed-delivery/) for `managed.json`.

## Common Options

| Option | Meaning |
| --- | --- |
| `--config PATH` | Use this `agent.json` |
| `--runtime-dir PATH` | Directory for `agent.pid` and `agent.log` |
| `--system` | Use system configuration and runtime locations |
| `--daemonize` | Run in the background (user operation only) |
| `--status` | Report the running agent and its generation, then exit |

`--system --daemonize` and `--status --daemonize` are rejected.

---

## Status

```sh
detent-agent --status --generation-dir ./agent-state
# running (pid 65214)
# durable revision=3 knob_count=2
```

`--status --generation-dir PATH` reports the durable revision and count
independently of whether the process is running.

---

## Environment

| Variable | Meaning |
| --- | --- |
| `DETENT_MANAGED_CONFIG` | Private managed configuration |
| `DETENT_GENERATION_DIR` | Generation directory |
| `DETENT_DEFAULTS_PATH` | Defaults file |
| `DETENT_RUNTIME_DIR` | Runtime directory |

---

## Files

| File | Location | Purpose |
| --- | --- | --- |
| `agent.json` | Config directory | Optional configuration |
| `defaults.json` | Beside `agent.json` | Optional local fallback knobs, discovered automatically |
| `agent.pid` | Runtime directory | Lock and PID |
| `agent.log` | Runtime directory | Background (`--daemonize`) logs |

The runtime directory defaults to `$XDG_RUNTIME_DIR/detent`, else a private
`detent-<uid>` directory under `$TMPDIR` (mode `0700`); system agents use
`/run/detent` on Linux and `/var/run/detent` on BSD and macOS.
