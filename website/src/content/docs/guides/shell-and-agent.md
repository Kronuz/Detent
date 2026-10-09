---
title: "Shell Scripts and the Agent"
---

Not every process that needs a knob is a long-running C++ service. Cron jobs,
deploy scripts, health checks and programs in other languages can read knobs
through **`detent`**, the shell reader, backed by **`detent-agent`**, one small
daemon per host.

---

## How It Works

```d2 alt="detent-agent polls the distributor once for the whole host and publishes a pre-parsed snapshot into a shared-memory file; every detent get maps that file read-only"
direction: down
detentd: "detentd\n(remote or local)"
agent: "detent-agent\n(one warm client)"
shm: "snapshot.shm\n(pre-parsed binary,\nseqlock-guarded)" {shape: cylinder}
a: "detent get NAME\n(shell script)"
b: "detent get NAME\n(any language)"
detentd -> agent: "GET /v1/snapshot\n(one poller per host)"
agent -> shm: "publish on every\naccepted update"
shm -> a: "mmap read-only"
shm -> b: "mmap read-only"
```

The agent runs one ordinary `detent::Client`. On every accepted update it
serializes the snapshot into a flat binary layout in a memory-mapped file.
`detent get` maps that file read-only and evaluates directly against it: no
socket, no request to the daemon, no JSON parsing.

---

## Reading from the Shell

```sh
detent get search:max_results
# 25
detent get payments:new_processor --hash user-42
# true
detent get payments:new_processor --hash user-2 --switch employees
# true
```

The first `get` starts the agent automatically if it isn't running, connects it
to `127.0.0.1:8317`, and waits up to two seconds for the first snapshot.

| Exit status | Meaning |
| --- | --- |
| `0` | The value was printed to stdout (`true`, `false` or an integer) |
| `1` | The knob couldn't be read; the reason is printed, e.g. `ERR MissingKnob: checkout:nope (not present in current snapshot)` |

For scripts that must keep going, `--default` supplies a fallback for that one
read:

```sh
if [ "$(detent get checkout:new_path --default false)" = true ]; then
  run_new_path
fi
```

---

## Pointing the Agent Elsewhere

For a distributor that isn't on `127.0.0.1:8317`, write `agent.json` in your
config directory:

| Platform | User config directory |
| --- | --- |
| Linux, FreeBSD | `$XDG_CONFIG_HOME/detent`, otherwise `~/.config/detent` |
| macOS | `~/Library/Application Support/Detent` |

```json title="agent.json"
{
  "source_address": "distributor.internal:8317",
  "poll_interval_ms": 200
}
```

Then plain `detent get NAME` works from any directory. You can also pass
`--source-address HOST:PORT` or set `DETENT_SOURCE_ADDRESS`; flags override
environment variables, which override the config file.

To serve a local snapshot file instead of a distributor, use `source_path` /
`--source-path`. The two are mutually exclusive.

### Local Defaults

An optional `defaults.json` next to `agent.json` gives the agent typed fallback
knobs before the first update arrives, and for knobs the live snapshot doesn't
contain. With defaults and **no** source configured, the agent serves the
defaults without any network connection at all:

```sh
detent get search:max_results --defaults ./defaults.json
```

:::hint[Startup settings apply to a new agent]{.caution}
Flags like `--source-address` and `--defaults` configure an agent when `get`
starts one. If an agent is already running, stop it to apply changed settings.
:::

---

## Managing the Agent

```sh
detent-agent --status
# running (pid 65214)
# OK version=3 knob_count=2
```

`--status` never starts anything. Stop an unmanaged agent with SIGTERM:

```sh
kill -TERM "$(detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

The agent keeps its runtime files (`agent.pid`, `agent.log`, `snapshot.shm`)
in `$XDG_RUNTIME_DIR/detent`, or a private `detent-<uid>` directory under the
system temporary directory, created with mode `0700`. Use the same
`--runtime-dir` for `get` and `--status` if you override it.

On servers, run the agent under the service manager instead; see
[System Services](/Detent/operations/services/). System readers use `--system`,
which attaches to the managed agent and never starts a private one:

```sh
detent get search:max_results --system
```

---

## Performance

| Benchmark | Result |
| --- | --- |
| `detent_read_bench 4 2000000` (in-process `Client`) | 6.71M ops/s (149.1 ns/op) |
| `detent_agent_shm_bench 4 2000000` (shared-memory reader) | 36.2M ops/s (27.6 ns/op) |

The shared-memory lookup itself is faster than an in-process client read. A
`detent get` from a shell still pays for starting a process, which dominates its
wall time.

:::hint[Knob values are visible to local users]{.warning}
A system agent's shared-memory file is world-readable (`0644`) so every local
account can read knobs. Never put secrets in knobs.
:::
