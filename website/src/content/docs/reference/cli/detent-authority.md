---
title: "detent-authority"
---

```text
detent-authority --authority CONFIG [--daemonize | --status] [--runtime-dir PATH] [--config PATH]
                 [--port N] [--reactors N] [--max-clients N] [--max-in-flight N]
                 [--idle-timeout-ms MS] [--shutdown-timeout-ms MS] [--upstream-ca FILE] [--system]
detent-authority --status [--port N]
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
| `--max-clients N` | `1024` | Open connections; the process raises its file-descriptor limit to fit |
| `--max-in-flight N` | `1024` | Admitted requests per reactor |
| `--idle-timeout-ms MS` | `75000` | Close an idle connection after this long (`0` = never) |
| `--shutdown-timeout-ms MS` | `35000` | Bound on finishing in-flight requests after SIGTERM |
| `--upstream-ca FILE` | the publisher's CA | CA the `--status` probe verifies against |
| `--config PATH` | platform config dir | Use this `detent-authority.json` |
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
| `DETENT_AUTHORITY_PORT` | `port` |
| `DETENT_AUTHORITY_REACTORS` | `reactors` |
| `DETENT_AUTHORITY_RUNTIME_DIR` | `runtime_dir` |

---

## Configuration

`detent-authority.json` lives in the same directories as `agent.json`:

```json
{
  "authority": "/etc/detent/authority.json",
  "port": 8317,
  "reactors": 4,
  "max_clients": 1024,
  "max_in_flight": 1024,
  "idle_timeout_ms": 75000,
  "shutdown_timeout_ms": 35000,
  "upstream_ca": "/etc/detent/ca.pem",
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
detent-authority --status --port 8317
```

Without `--authority`, `--status` probes `127.0.0.1` over plain HTTP. With
`--authority CONFIG` it probes the publisher address in the configuration, and
checks the HTTPS endpoint with CA and name verification when the publisher uses
TLS. Either probe has a two-second deadline. A reachable instance prints its PID
and the health body and exits 0; otherwise `--status` prints `not reachable`
and exits 1.
