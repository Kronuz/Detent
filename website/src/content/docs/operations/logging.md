---
title: "Logging"
---

The distributor, agent and relay log to stderr with a severity marker and an ISO 8601 timestamp with milliseconds, using [Kronuz/logger](https://github.com/Kronuz/logger), the logging library extracted from Xapiand.

```text
▎2026-10-09 09:16:59.331 detentd: listening on :8317, persisting to /var/lib/detent/snapshot.json
▌2026-10-09 09:17:01.943 detentd: rejected publish (version)
▍2026-10-09 09:19:24.916 detent-agent: source connected
```

On a terminal, messages use Xapiand's severity tint and timestamps use a grey gradient. Tinting is the default; there is no separate tint option. Redirected output is plain unless color is forced.

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
| `--color truecolor\|256\|16` | Force RGB, 256-color or ANSI 16-color output, including when redirected |

The same settings work in `agent.json` and `detentd.json`; command-line flags win:

```json
{"log_level": "info", "verbosity": 0, "color": "auto"}
```

`always` and explicit color depths override `NO_COLOR`. As in Xapiand, `24bit`, `256color` and `ansi` alias the corresponding depths; `on`/`off` alias `always`/`never`. `stacked` preserves all three portable color tiers.

```sh
detentd --color truecolor -v >detent.log 2>&1
```

---

## What Gets Logged

1. Startup and shutdown of the daemons.
2. Rejected publishes, by category only, e.g. `rejected publish (schema)`, never their contents.
3. Agent source transitions: connected, unavailable, and refresh failures. Repeated failures drop to debug until a newer snapshot arrives.
4. Shared-memory capacity failures.
5. Classic distributor HTTP metadata through Xapiand's shared `http-log` middleware: method/path blocks and status/body-size blocks, with the same method and status palettes. Requests and successful/404 responses appear at debug, other 4xx at info, and 5xx at notice.

Hash values, switch values and knob values are never logged.

HTTP logging omits headers, bodies, query values and exception descriptions. Method and path labels are bounded and escaped. These logs describe the handler's response before HTTP transforms, not confirmed network delivery. Authority and relay HTTP access logging remains pending a shared completion observer for deferred replies and owned streams; their lifecycle logs already use the shared tint.

---

## Where Logs Go

| Mode | Destination |
| --- | --- |
| Foreground | stderr |
| `--daemonize` | `agent.log` (or a per-port distributor log) in the runtime directory |
| systemd | The journal |
| launchd, rc.d | `/var/log/detent` |

Logging drains before a normal exit, including on SIGTERM. Routine messages are queued with a 4,096-entry limit; warnings and errors bypass it and critical messages are written synchronously.

---

## Footprint

The original logger addition cost `detentd` 74,448 stripped bytes and one scheduler thread, and `detent-agent` 88,280 bytes. The `detent` reader doesn't link the logger at all. See [Benchmarks](/Detent/benchmarks/#footprint).

The later color-depth and HTTP metadata addition costs 57,552 unstripped bytes (2.68%) in the isolated classic distributor profile and 88 bytes in the agent; this is a separate comparison with different build settings. [Recorded qualification](https://github.com/Kronuz/Detent/blob/main/benchmarks/xapiand-logging-preliminary-20261020.json) preserves the coherent public pins and clean product build.

The [accepted exploratory comparison](https://github.com/Kronuz/Detent/blob/main/benchmarks/logging-cost-comparison-20261021.json) uses three alternating pairs per mode with 100k knobs and 20k conditional polls. Default-filtered CPU medians are 0.51/0.52 seconds, one counter tick apart; the paired median change is zero with substantial variation. Verbose logging adds about 47–47.5 CPU seconds per million polls (0.013 core-hours), and every verbose candidate retains all request and response entries. The component review accepts the file-size exception. These measurements establish neither zero default overhead nor an incremental RGB cost. Allocation, peak memory and fleet capacity remain open.
