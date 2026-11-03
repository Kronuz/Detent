---
title: "detent"
---

```text
detent get NAME [--hash VAL] [--switch VAL] [--default VAL]
                [--config PATH] [--defaults PATH] [--runtime-dir PATH] [--system]
                [--generation-dir PATH] [--managed PRIVATE_CONFIG]
detent help | --help
```

`detent get` reads one knob and prints its value: `true`, `false`, or an
integer. It maps the agent's selected generation read-only and evaluates
locally; it doesn't send a request to the agent.

---

## Options

| Option | Meaning |
| --- | --- |
| `--hash VAL` | Hash value for rollouts (a stable user, account or host ID) |
| `--switch VAL` | Switch value for group overrides |
| `--default VAL` | Fallback printed if the knob can't be read; publishes nothing |
| `--config PATH` | Use this `agent.json` |
| `--defaults PATH` | Typed fallback knobs |
| `--runtime-dir PATH` | Runtime directory holding the agent's pid and log |
| `--system` | Attach to the system agent; never start one |
| `--generation-dir PATH` | Read a managed agent's durable generation directory |
| `--managed PRIVATE_CONFIG` | Managed credential file for a newly started agent |
| `--generation-cluster`, `--generation-group`, `--generation-epoch`, `--generation-scope` | Expected managed identity; all four or none |

---

## Behavior

1. If no generation is available and a managed configuration is present,
   `get` starts an agent with the given startup settings and waits for its
   first generation.
2. With `--system`, `get` attaches to the service-managed agent. If it isn't
   running, `get` fails and says the service must be started.
3. With `--generation-dir`, `get` reads the selected durable generation, even
   while the managed agent is stopped.
4. The reader starts a sibling `detent-agent` binary, or finds it on `PATH`.

---

## Output and Exit Status

| Situation | Stdout | Exit status |
| --- | --- | --- |
| Read succeeded | `true`, `false` or an integer | `0` |
| Read failed, `--default` given | The default | `0` |
| Read failed | `ERR <Category>: <name> (<detail>)` | `1` |
| Agent couldn't start | An error on stderr | `1` |
| Unknown option, missing option value or extra positional argument | One argument error on stderr, without starting an agent or using `--default` | `1` |

```sh
detent get payments:new_processor --hash user-42 --switch beta
# true
detent get checkout:nope
# ERR MissingKnob: checkout:nope (not present in current snapshot)
detent get checkout:nope --default false
# false
```

---

## Environment

`DETENT_MANAGED_CONFIG`, `DETENT_GENERATION_DIR`, `DETENT_DEFAULTS_PATH` and
`DETENT_RUNTIME_DIR` configure a newly started agent; see [`detent-agent`](/Detent/reference/cli/detent-agent/#environment).
