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

### Service State

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
  "managed_path": "managed.json",
  "generation_dir": "agent-state",
  "defaults_path": "defaults.json",
  "runtime_dir": "/run/detent",
  "log_level": "info",
  "verbosity": 0,
  "color": "auto"
}
```

| Field | Meaning |
| --- | --- |
| `defaults_path` | Optional fallback knobs; `defaults.json` beside the config is found automatically |
| `runtime_dir` | Runtime directory |
| `managed_path` | Private managed credential file |
| `generation_dir` | Managed generation directory |
| `log_level`, `verbosity`, `color` | [Logging](/Detent/operations/logging/) |

An explicitly configured defaults file must exist and be valid; an absent
conventional `defaults.json` is skipped.

## `detent-authority.json`

| Field | Default | Meaning |
| --- | --- | --- |
| `port` | `8317` | HTTP port |
| `authority` | none | The [authority configuration](/Detent/operations/authority/) file `detent-authority` serves |
| `reactors` | `4` | Network reactor threads |
| `runtime_dir` | runtime directory | PID and log files |
| `log_level`, `verbosity`, `color` | | [Logging](/Detent/operations/logging/) |

## `managed.json`

The managed agent's private credential file. See
[Managed Delivery](/Detent/guides/managed-delivery/) for every field and its
resource bounds.

`compaction_interval_ms` defaults to 60,000. Zero disables periodic compaction; enabled values range from 1,000 to 86,400,000. Checks run between upstream exchanges, and admission pressure skips an optional attempt. Reader leases and durable collection determine when old storage can be reclaimed.

## `authority.json`

The authority's private configuration. See
[Operations: Durable Authority](/Detent/operations/authority/#configuration-reference).

---

## Authority Subscription Limits

The private authority configuration accepts an optional `subscription_limits` object. Unknown fields are rejected. Session controls use separate admission from publishers.

| Field | Default | Maximum |
| --- | --- | --- |
| `sessions` | 1,024 | 65,536 |
| `per_principal` | 256 | 65,536 |
| `idle_ms` | 60,000 | 86,400,000 |
| `maximum_ms` | 600,000 | 86,400,000 |
| `opens` | 16 | 128 |
| `polls` | 256 | 512 |
| `closes` | 16 | 128 |
| `per_principal_controls` | 8 | 65,536 |
| `per_principal_polls` | 256 | 65,536 |
| `open_bytes` | 262,144 | 1,048,576 |
| `control_bytes` | 4,096 | 16,384 |
| `wait_ms` | 30,000 | 60,000 |

Counts and byte bounds must be positive. `wait_ms` may be zero. Maximum lifetime must cover idle lifetime. Actual storage admission also depends on the delivery ledger; raising logical limits does not reserve unlimited memory.

`application_limits.layout` sets the history layout policy that the enabling entry proposes. It is validated the same way as an administrative layout change. The consensus log limits always come from `consensus_limits`.

| Field | Default | Meaning |
| --- | --- | --- |
| `seal_transactions` | 2,048 | Positioned changes per seal |
| `seal_bytes` | 8,388,608 | Positioned bytes per seal |
| `period` | 64 | Seals between a namespace's roll-ups |
| `carry_bytes`, `carry_count` | 4,096 and 2 | A roll-up this small stays a section |
| `ratio` | 4 | Squash ratio between adjacent blocks |
| `alpha_num`, `alpha_den` | 1 and 8 | The block limit, as a fraction of a namespace's live bytes |
| `cap` | 2 | Disk cap, in units of the block limit |

The private managed agent configuration names the followed `namespaces` (every granted namespace when absent) and follows them over one W5 session; the server negotiates long-poll duration. Failed exchanges use exponential reconnect backoff with equal jitter, starting at `retry_interval_ms` (default 200) and capped by `reconnect_maximum_ms` (default 30,000, or the retry interval when larger). The cap must cover the retry interval and cannot exceed 86,400,000 ms. Each coordinator seeds its schedule independently; authenticated idle responses and durable publication reset backoff. Successful long polls and disk completion waits are unchanged.

---

## Environment Variables

| Variable | Used by | Meaning |
| --- | --- | --- |
| `DETENT_MANAGED_CONFIG` | agent, `detent` | Private managed configuration |
| `DETENT_GENERATION_DIR` | agent, `detent` | Durable generation directory |
| `DETENT_DEFAULTS_PATH` | `detent` | Defaults file |
| `DETENT_RUNTIME_DIR` | agent, `detent` | Runtime directory |
| `DETENT_AUTHORITY_PORT` | `detent-authority` | Port |
| `DETENT_AUTHORITY_REACTORS` | `detent-authority` | Reactor threads |
| `NO_COLOR` | daemons | Disable automatic color |
| `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `TMPDIR` | all | Base directories |

---

## File Permissions

| File | Owner | Mode |
| --- | --- | --- |
| `agent.json`, `detent-authority.json` | root | readable by the service account |
| `managed.json`, `authority.json` | root or service account | `0600`, or controlled `0640` |
| Authority state | service account | `0700` |
| System agent runtime directory | service account | `0755`, with `0644` pid and snapshot |
| Managed generation artifacts | service account | `0644` by default for system agents |

Private configuration must be a regular, single-link file without world access
or group/other write permission; Detent refuses it otherwise.

## Relay admission

The private relay host configuration accepts `connections` (default 1,024) and `service_limits` with the requests in flight per lane: `opens` (16; the history routes share it), `polls` (640) and `closes` (16), each 1 to 65,536. See the [relay guide](https://github.com/Kronuz/Detent/blob/main/docs/RELAY.md#capacity) for the heap budget and a complete example.

## Relay namespace mirror

A relay configuration requires a `mirror` object: the relay mirrors namespace history from the authority's voters, or from another relay, and serves it to version 2 sessions and to downstream relays. Each of its credentials grants `namespaces` patterns.

| Field | Default | Meaning |
| --- | --- | --- |
| `directory` | required | The mirror's own directory, mode `0700` |
| `cluster` | required | The authority's cluster identity, 32 hex digits |
| `namespaces` | required | The names mirrored, in increasing order, at most 4,096 |
| `upstreams` | required | Up to 64 `{"address", "port"}` objects, read in turn: a failure, or an answer older than the mirror, moves to the next |
| `token` | required | The relay's credential at its upstream |
| `hostname`, `ca` | `localhost`, the relay's `ca` | How the relay verifies its upstream |
| `interval_ms` | 250 | Time between syncs, 1 to 60,000; a failed sync doubles it, up to 30 seconds or one interval |
| `timeout_ms` | 10,000 | Deadline for each upstream request |
| `compression` | `false` | Read W6 answers in zstd |
| `sessions` | 1,024 | Sessions the relay holds |
| `answer_bytes` | 67,108,864 | Memory for answers in flight, 1 MiB to 4 GiB |

## Managed authority endpoint pools

A managed source can set `authority_endpoints` to an array of up to eight objects such as `[{"address":"127.0.0.1","port":8317},{"address":"127.0.0.2","port":8317}]`, replacing the top-level `address` and `port`. Destinations must be distinct IP addresses with nonzero ports. All entries share the configured TLS hostname, CA, credential, authority identity and namespace scope.

The coordinator tries members serially and stays on a successful member. Transport failures, unavailable authorities and follower read rejection permit rotation. Admission pressure and local storage failures retain the route. Authentication, authorization, TLS verification and unexpected identity or profile fail closed, including denials received while closing a session. Sessions and publication feedback remain bound to their opening endpoint.

For pooled subscriptions, `quorum_refresh_ms` defaults to 30000 and accepts 1000 through 86400000. Refresh uses bounded jitter of ±20%; idle responses do not postpone it. A fresh Open revalidates authority quorum after accepted local publication and recovery settle. Polling already performs quorum reads. Discovery time includes refresh, per-member request deadlines, retry backoff and accepted local work; the default 60-second request timeout provides no short failover guarantee.

Pools contain authority endpoints. Relay Open does not establish current authority quorum. An agent consuming a relay must use a single relay endpoint and explicit `"delivery_mode":"subscription"`.
