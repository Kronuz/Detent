---
title: "Configuration"
---

Every Detent setting has a built-in default, so nothing needs configuring for a
local setup. This page collects every file, directory and environment variable.

---

## Precedence

```text
command-line flag  >  environment variable  >  configuration file  >  built-in default
```

User configuration takes precedence over system configuration; `--config PATH`
selects a file explicitly. Relative paths inside a configuration file are
resolved against the file's own directory. Unknown fields are rejected.

---

## Directories

### Configuration

| Platform | User | System (`--system`) |
| --- | --- | --- |
| Linux | `$XDG_CONFIG_HOME/detent`, else `~/.config/detent` | `/etc/detent` |
| FreeBSD | `$XDG_CONFIG_HOME/detent`, else `~/.config/detent` | `/usr/local/etc/detent` |
| macOS | `~/Library/Application Support/Detent` | `/Library/Application Support/Detent` |

### Distributor State

| Platform | User | System |
| --- | --- | --- |
| Linux | `$XDG_STATE_HOME/detent`, else `~/.local/state/detent` | `/var/lib/detent` |
| FreeBSD | `$XDG_STATE_HOME/detent`, else `~/.local/state/detent` | `/var/db/detent` |
| macOS | `~/Library/Application Support/Detent/state` | `/var/db/detent` |

### Agent Runtime

| Platform | User | System |
| --- | --- | --- |
| All | `$XDG_RUNTIME_DIR/detent`, else `$TMPDIR/detent-<uid>` (mode `0700`) | Linux `/run/detent`, BSD/macOS `/var/run/detent` |

### Managed Agent State

| Platform | System |
| --- | --- |
| Linux | `/var/lib/detent-agent` |
| FreeBSD, macOS | `/var/db/detent-agent` |

On macOS, an absolute `XDG_*` variable overrides the native location too.

---

## `agent.json`

```json
{
  "source_address": "127.0.0.1:8317",
  "poll_interval_ms": 200,
  "defaults_path": "defaults.json",
  "runtime_dir": "/run/detent",
  "log_level": "info",
  "verbosity": 0,
  "color": "auto"
}
```

| Field | Meaning |
| --- | --- |
| `source_address` | Distributor `host:port` (legacy alias: `source_http`) |
| `source_path` | Snapshot file; exclusive with `source_address` |
| `defaults_path` | Optional fallback knobs; `defaults.json` beside the config is found automatically |
| `poll_interval_ms` | Poll interval, default `200` |
| `runtime_dir` | Runtime directory |
| `managed_path` | Private managed credential file |
| `generation_dir` | Managed generation directory |
| `generation_cluster`, `generation_group`, `generation_epoch`, `generation_scope` | Expected managed identity; all four or none |
| `log_level`, `verbosity`, `color` | [Logging](/Detent/operations/logging/) |

An explicitly configured defaults file must exist and be valid; an absent
conventional `defaults.json` is skipped.

## `detentd.json`

| Field | Default | Meaning |
| --- | --- | --- |
| `port` | `8317` | HTTP port |
| `persist_path` | state directory | Snapshot persistence path |
| `reactors` | `4` | Network reactor threads |
| `workers` | `8` | HTTP worker threads |
| `runtime_dir` | runtime directory | PID and log files |
| `log_level`, `verbosity`, `color` | | [Logging](/Detent/operations/logging/) |

## `managed.json`

The managed agent's private credential file. See
[Managed Delivery](/Detent/guides/managed-delivery/) for every field and its
resource bounds.

## `authority.json`

The authority's private configuration. See
[Operations: Durable Authority](/Detent/operations/authority/#configuration-reference).

---

## Environment Variables

| Variable | Used by | Meaning |
| --- | --- | --- |
| `DETENT_SOURCE_ADDRESS` | agent, `detent` | Distributor `host:port` |
| `DETENT_SOURCE_HTTP` | agent, `detent` | Legacy alias of `DETENT_SOURCE_ADDRESS` |
| `DETENT_SOURCE_PATH` | agent, `detent` | Snapshot file |
| `DETENT_DEFAULTS_PATH` | agent, `detent` | Defaults file |
| `DETENT_POLL_INTERVAL_MS` | agent, `detent` | Poll interval |
| `DETENT_RUNTIME_DIR` | agent, `detent` | Runtime directory |
| `DETENTD_PORT` | `detentd` | Port |
| `DETENTD_PERSIST_PATH` | `detentd` | Persistence path |
| `DETENTD_REACTORS` | `detentd` | Reactor threads |
| `DETENTD_WORKERS` | `detentd` | Worker threads |
| `NO_COLOR` | daemons | Disable automatic color |
| `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `TMPDIR` | all | Base directories |

---

## File Permissions

| File | Owner | Mode |
| --- | --- | --- |
| `agent.json`, `detentd.json` | root | readable by the service account |
| `managed.json`, `authority.json` | root or service account | `0600`, or controlled `0640` |
| Distributor and authority state | service account | `0700` |
| System agent runtime directory | service account | `0755`, with `0644` pid and snapshot |
| Managed generation artifacts | service account | `0644` by default for system agents |

Private configuration must be a regular, single-link file without world access
or group/other write permission; Detent refuses it otherwise.
