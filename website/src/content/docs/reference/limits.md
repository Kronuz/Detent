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

## Client and Classic Distributor

| Limit | Value | Configurable |
| --- | --- | --- |
| Knobs per snapshot | 100,000 | `InitOptions::max_knobs` |
| Snapshot or request size | 16 MiB | no |
| Retained diff history | 64 versions | no |
| Minimum compressed body | 256 bytes | no |
| Poll interval | must be positive | `poll_interval` |

## Agent

| Limit | Value |
| --- | --- |
| Legacy shared-memory mapping | 4 MiB |
| Wait for first snapshot in `detent get` | 2 seconds |

:::hint[The legacy agent's 4 MiB mapping]{.caution}
The legacy agent publishes into a fixed 4 MiB shared-memory file. A snapshot
that doesn't fit is reported as a capacity failure and isn't published; readers
keep the previous one. The 100,000-knob synthetic corpus encodes to about
9 MB, so large configurations need the managed agent, whose immutable
generations are sized dynamically.
:::

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
log bytes, configurable downward through `consensus_limits`.
