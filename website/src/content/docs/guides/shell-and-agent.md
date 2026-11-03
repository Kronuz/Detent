---
title: "Shell Scripts and the Agent"
---

Not every process that needs a knob is a long-running C++ service. Cron jobs,
deploy scripts, health checks and programs in other languages can read knobs
through **`detent`**, the shell reader, backed by **`detent-agent`**, one small
daemon per host.

---

## How It Works

```d2 alt="detent-agent receives snapshots from the authority or a relay once for the whole host and publishes immutable generations; every detent get maps the selected generation read-only"
direction: down
upstream: "authority or relay"
agent: "detent-agent\n(managed)"
gen: "generation_dir\n(selected + immutable\ngenerations)" {shape: cylinder}
a: "detent get NAME\n(shell script)"
b: "detent get NAME\n(any language)"
upstream -> agent: "snapshots and deltas\n(one connection per host)"
agent -> gen: "publish on every\naccepted update"
gen -> a: "mmap read-only"
gen -> b: "mmap read-only"
```

On every accepted update the agent builds a new immutable generation and
selects it atomically. `detent get` maps the selected generation read-only and
evaluates directly against it: no socket, no request to the daemon, no JSON
parsing.

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

With a managed configuration, the first `get` starts the agent automatically if
it isn't running and waits for the first generation. `get` reads the selected
generation directly, even while the agent is stopped.

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

## Configuring the Agent

Write `agent.json` in your config directory so plain `detent get NAME` works
from any directory:

| Platform | User config directory |
| --- | --- |
| Linux, FreeBSD | `$XDG_CONFIG_HOME/detent`, otherwise `~/.config/detent` |
| macOS | `~/Library/Application Support/Detent` |

```json title="agent.json"
{
  "managed_path": "managed.json",
  "generation_dir": "agent-state"
}
```

Relative paths resolve against the config file's directory. You can also pass
`--managed`, `--generation-dir` and `--config PATH`; flags override environment
variables, which override the config file. See
[Managed Delivery](/Detent/guides/managed-delivery/) for the private
configuration.

### Local Defaults

An optional `defaults.json` next to `agent.json` gives typed fallback knobs
when no generation is available, and for knobs the generation doesn't contain.
With defaults and **no** generation directory configured, `get` serves the
defaults alone:

```sh
detent get search:max_results --defaults ./defaults.json
```

:::hint[Startup settings apply to a new agent]{.caution}
Flags like `--managed` and `--defaults` configure an agent when `get` starts
one. If an agent is already running, stop it to apply changed settings.
:::

---

## Managing the Agent

```sh
detent-agent --status --generation-dir ./agent-state
# running (pid 65214)
# durable revision=3 knob_count=2
```

`--status` never starts anything. Stop a user agent with SIGTERM:

```sh
kill -TERM "$(detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

The agent keeps its runtime files (`agent.pid`, `agent.log`) in
`$XDG_RUNTIME_DIR/detent`, or a private `detent-<uid>` directory under the
system temporary directory, created with mode `0700`. Use the same
`--runtime-dir` for `get` and `--status` if you override it.

On servers, run the agent under the service manager instead; see
[System Services](/Detent/operations/services/). System readers use `--system`,
which attaches to the managed agent and never starts a private one:

```sh
detent get search:max_results --system
```

:::hint[Knob values are visible to local users]{.warning}
A system agent's generation files are world-readable (`0644` by default) so
every local account can read knobs. Never put secrets in knobs.
:::
