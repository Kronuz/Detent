---
title: "detentd"
---

```text
detentd --authority CONFIG [--daemonize | --status] [--runtime-dir PATH] [--config PATH]
        [--port N] [--reactors N] [--system]
detentd --status [--port N]
```

The [durable authority](/Detent/guides/durable-authority/) daemon. It reads the
authority configuration at `CONFIG` and listens on port `8317` unless
`--port` says otherwise (`0` binds a free port and logs it).

---

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--authority CONFIG` | required | The authority configuration file |
| `--port N` | `8317` | HTTP port |
| `--reactors N` | `4` | Network reactor threads |
| `--config PATH` | platform config dir | Use this `detentd.json` |
| `--runtime-dir PATH` | platform runtime dir | Where pid and log files go |
| `--system` | | System config and runtime directories |
| `--daemonize` | | Run in the background |
| `--status` | | Query the local instance's health and PID, then exit |

PID and log filenames include the port, so several instances can share one
runtime directory.

---

## Environment

| Variable | Overrides |
| --- | --- |
| `DETENTD_PORT` | `port` |
| `DETENTD_REACTORS` | `reactors` |
| `DETENTD_RUNTIME_DIR` | `runtime_dir` |

---

## Configuration

`detentd.json` lives in the same directories as `agent.json`:

```json
{
  "authority": "/etc/detent/authority.json",
  "port": 8317,
  "reactors": 4,
  "runtime_dir": "/run/detent",
  "log_level": "info",
  "verbosity": 0,
  "color": "auto"
}
```

All fields are optional except that `--authority` or the `authority` field must
name a configuration to serve. Paths are relative to the file.

---

## Status

```sh
detentd --status --port 8317
```

Without `--authority`, `--status` probes `127.0.0.1` over plain HTTP. With
`--authority CONFIG` it probes the publisher address in the configuration, and
checks the HTTPS endpoint with CA and name verification when the publisher uses
TLS. Either probe has a two-second deadline. A reachable instance prints its PID
and the health body and exits 0; otherwise `--status` prints `not reachable`
and exits 1.
