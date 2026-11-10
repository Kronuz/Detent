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
integer. It maps the managed agent's published generation read-only and
evaluates locally; it doesn't send a request to the agent. With neither
`--managed` nor `--generation-dir` (or `managed_path` and `generation_dir` in
`agent.json`), it fails with `no source configured`.

---

## Options

| Option | Meaning |
| --- | --- |
| `--hash VAL` | Hash value for rollouts (a stable user, account or host ID) |
| `--switch VAL` | Switch value for group overrides |
| `--default VAL` | Fallback printed if the knob can't be read; publishes nothing |
| `--config PATH` | Use this `agent.json` |
| `--defaults PATH` | Typed fallback knobs when no generation is present or one omits a knob |
| `--runtime-dir PATH` | Runtime directory holding the agent's pid and log |
| `--system` | Attach to the system agent; never start one |
| `--generation-dir PATH` | Read a managed agent's durable generation directory |
| `--managed PRIVATE_CONFIG` | Managed credential file for a newly started managed agent |
| `--generation-cluster`, `--generation-group`, `--generation-epoch`, `--generation-scope` | Expected managed identity; all four or none |

---

## Behavior

1. `get` reads the selected durable generation in the generation directory,
   even while the managed agent is stopped.
2. If no generation is present and `--managed` is set, `get` starts the agent
   (a sibling `detent-agent` binary, or one on `PATH`) with the given startup
   settings and waits up to three minutes for the first generation.
3. With `--system`, `get` attaches to the service-managed agent and never
   starts one.
4. If no generation arrives, `get` answers from `--defaults` when it can, and
   otherwise fails.

---

## Output and Exit Status

| Situation | Stdout | Exit status |
| --- | --- | --- |
| Read succeeded | `true`, `false` or an integer | `0` |
| Read failed, `--default` given | The default | `0` |
| Read failed | `ERR <Category>: <name> (<detail>)` | `1` |
| Agent couldn't start, or no source configured | An error on stderr | `1` |
| Unknown option, missing option value or extra positional argument | One argument error on stderr, without starting an agent or using `--default` | `1` |

```sh
detent get payments:new_processor --hash user-42 --switch beta
# true
detent get checkout:nope
# ERR MissingKnob: checkout:nope (not present in current snapshot)
detent get checkout:nope --default false
# false
```

<!-- e2e:begin
stdout:
  true
  ERR MissingKnob: checkout:nope (not present in current snapshot)
  false
e2e:end -->

---

## Environment

`DETENT_MANAGED_CONFIG`, `DETENT_GENERATION_DIR`, `DETENT_DEFAULTS_PATH` and
`DETENT_RUNTIME_DIR` configure a newly started agent; see [`detent-agent`](/Detent/reference/cli/detent-agent/#environment).
