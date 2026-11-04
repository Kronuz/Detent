---
title: "Shell Scripts and the Agent"
---

Not every process that needs a knob is a long-running C++ service. Cron jobs,
deploy scripts, health checks and programs in other languages can read knobs
through **`detent`**, the shell reader, backed by **`detent-agent`**, one small
daemon per host.

---

## How It Works

```d2 alt="detent-agent receives deliveries from the authority once for the whole host and publishes immutable generations on disk; every detent get maps the selected generation read-only"
direction: down
source: "Authority or relay"
agent: "detent-agent\n(managed delivery)"
store: "generation_dir\n(immutable images,\nselected descriptor)" {shape: cylinder}
a: "detent get NAME\n(shell script)"
b: "detent get NAME\n(any language)"
source -> agent: "TLS delivery\n(one connection per host)"
agent -> store: "publish on every\naccepted update"
store -> a: "mmap read-only"
store -> b: "mmap read-only"
```

The agent receives snapshots and deltas over verified TLS, builds a new
immutable image for each, and selects it atomically. `detent get` maps the
selected image read-only and evaluates directly against it: no socket, no
request to the daemon, no JSON parsing. A generation stays readable with the
agent stopped.

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

With `--managed PRIVATE_CONFIG` (or `managed_path` in `agent.json`), the first
`get` starts the agent if no generation is present and waits for the first one.
With neither `--managed` nor `--generation-dir` configured, `get` fails with
`no source configured`; `--default` doesn't mask that.

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

## Pointing the Agent at the Authority

Write `agent.json` in your config directory:

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

Then plain `detent get NAME` works from any directory. You can also pass
`--managed PATH` and `--generation-dir PATH`, or set `DETENT_MANAGED_CONFIG`
and `DETENT_GENERATION_DIR`; flags override environment variables, which
override the config file. [Managed Delivery](/Detent/guides/managed-delivery/)
covers `managed.json`.

### Local Defaults

An optional `defaults.json` next to `agent.json` gives `detent get` typed
fallback knobs while no generation has arrived, and for knobs a generation
doesn't contain:

```sh
detent get search:max_results --defaults ./defaults.json
```

:::hint[Startup settings apply to a new agent]{.caution}
Flags like `--managed` configure an agent when `get` starts one. If an agent is
already running, stop it to apply changed settings.
:::

---

## Managing the Agent

```sh
detent-agent --status --managed managed.json
# running (pid 65214)
# durable revision=3 knob_count=2
```

`--status` never starts anything. Stop a user agent with SIGTERM:

```sh
kill -TERM "$(detent-agent --status --managed managed.json | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
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
Any local account that can read the generation directory can read every
published knob. Never put secrets in knobs.
:::
