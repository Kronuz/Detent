---
title: "Logging"
---

Both daemons log to stderr with a severity marker and an ISO 8601 timestamp
with milliseconds, using [Kronuz/logger](https://github.com/Kronuz/logger),
the logging library extracted from Xapiand.

```text
▎2026-10-09 09:16:59.331 detentd: listening on :8317, persisting to /var/lib/detent/snapshot.json
▌2026-10-09 09:17:01.943 detentd: rejected publish (version)
▍2026-10-09 09:19:24.916 detent-agent: source connected
```

On a terminal, output is colored by severity; redirected output is plain.

---

## Verbosity

```sh
detentd                         # lifecycle messages and warnings (default: info)
detentd -v                      # also accepted snapshots
detent-agent -v                 # also agent publication details
detent-agent -vv                # also source metric samples, every 200 ms
detentd --log-level warning     # quiet service logs
detentd --color never           # plain output even on a terminal
```

| Flag | Effect |
| --- | --- |
| `--log-level LEVEL` | `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info`, `debug` |
| `-v`, `-vv`, `--verbosity 0..2` | Raise the level |
| `--color auto\|always\|never`, `--no-color` | Color policy; `auto` honors `NO_COLOR` |

The same settings work in `agent.json` and `detentd.json`; command-line flags
win:

```json
{"log_level": "info", "verbosity": 0, "color": "auto"}
```

---

## What Gets Logged

1. Startup and shutdown of both daemons.
2. Rejected publishes, by category only, e.g. `rejected publish (schema)`,
   never their contents.
3. Agent source transitions: connected, unavailable, and refresh failures.
   Repeated failures drop to debug until a newer snapshot arrives.
4. Shared-memory capacity failures.

Hash values, switch values and knob values are never logged.

---

## Where Logs Go

| Mode | Destination |
| --- | --- |
| Foreground | stderr |
| `--daemonize` | `agent.log` (or a per-port distributor log) in the runtime directory |
| systemd | The journal |
| launchd, rc.d | `/var/log/detent` |

Logging drains before a normal exit, including on SIGTERM. Routine messages are
queued with a 4,096-entry limit; warnings and errors bypass it and critical
messages are written synchronously.

---

## Footprint

Adding the logger cost `detentd` 74,448 stripped bytes and one scheduler
thread, and `detent-agent` 88,280 bytes. The `detent` reader doesn't link the
logger at all. See [Benchmarks](/Detent/benchmarks/#footprint).
