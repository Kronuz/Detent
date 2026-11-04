---
title: "Limits"
---

Every input Detent accepts is bounded before it's buffered or allocated. These
are the defaults.

---

## Knob Model

| Limit | Value |
| --- | --- |
| Knob name length | 1–200 bytes |
| Group name length | 1–128 bytes |
| `rollout_pct` | 0–100 |
| Int knob range | signed 64-bit |

## Client

| Limit | Value | Configurable |
| --- | --- | --- |
| Knobs per snapshot | 100,000 | `InitOptions::max_knobs` |
| Snapshot size | 16 MiB | no |
| Poll interval | must be positive | `poll_interval` |

## Agent

| Limit | Value |
| --- | --- |
| Wait for the first generation in `detent get` | 3 minutes (the managed startup timeout) |

The managed agent's immutable generations are sized dynamically; see the limits
below.

## Managed Agent

| Limit | Default |
| --- | --- |
| Managed heap | 256 MiB |
| Mapped virtual space | 512 MiB |
| Named disk space | 1 GiB |
| Disk entries | 4,096 |
| Generations | 1,024 |

## Local Image Format

| Limit | Value |
| --- | --- |
| Image size | 256 MiB |
| Records | 500,000 |
| Namespaces | 4,096 |
| String bytes | 128 MiB |
| Overrides per definition | 256 |

## Durable Authority

See the [Authority API limits](/Detent/reference/http-api/authority/#limits).
Consensus limits default to 65,536 retained log entries and 64 MiB of retained
log bytes, configurable downward through `consensus_limits`. The retained-state
envelope that recovery and checkpoints use keeps its own limits, set by
`retained_state`, and does not shrink with `consensus_limits`.
