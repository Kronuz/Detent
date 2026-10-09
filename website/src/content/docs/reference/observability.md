---
title: "Observability"
---

Detent exposes what you need to answer two questions: *is configuration
fresh?* and *are reads succeeding?*

---

## Client Health

```cpp
detent::HealthSnapshot h = client.health();
```

```json
{ "initialized": true, "source_connected": true,
  "accepted_version": 42, "staleness_seconds": 1.8,
  "bool_knobs": 812, "int_knobs": 64, "cli_overrides": 1 }
```

`staleness_seconds` is the monotonic time since the last successful update, or
since construction before the first. It's the single most useful signal: a
distribution outage shows up here long before anyone notices a stale flag.

## Client Metrics

`client.metrics()` returns process-wide atomic counters:

| Counter | Meaning |
| --- | --- |
| `reads_ok` | Successful reads |
| `reads_missing` | `MissingKnob` reads |
| `reads_wrong_type` | `WrongType` reads |
| `reads_conversion_failed` | `get_as` conversion failures |
| `snapshot_swaps` | Accepted updates published |
| `refresh_failures` | Failed source polls or rejected updates |
| `stale_updates` | Duplicate or older versions ignored |
| `source_connected` | Current source state |

Labels are deliberately absent: no knob name, hash value, switch value, path or
version ever becomes a metric dimension, so cardinality stays bounded and user
identifiers never leak into monitoring.

## Suggested Alerts

1. **Staleness.** `staleness_seconds` above `refresh_alert_after` (5 minutes by
   default). Tighten it for services whose kill switches must work within
   seconds.
2. **Refresh failures.** `refresh_failures` increasing with no `snapshot_swaps`
   following.
3. **Missing reads.** `reads_missing` above zero, unless your process already
   aborts on them through `_or_panic`.

:::hint[Metrics exporter]{.unimplemented}
Detent keeps these counters but doesn't export them. There's no Prometheus or
OpenMetrics endpoint on the daemons, and no exporter for application clients;
the specification's `jk_*`-style metric names, such as
`jk_snapshot_staleness_seconds`, are a natural mapping for one. For now, read
`health()` and `metrics()` and publish them through your own metrics library.
:::

---

## Daemon Health

1. `GET /v1/health` on the [distributor and authority](/Detent/reference/http-api/health/).
2. `detentd --status` and `detent-agent --status` from the command line.
3. Daemon logs, described in [Logging](/Detent/operations/logging/).

## What's Never Logged

Hash values, switch values and knob values are never logged, and rejected
publishes are logged by category without their payload. Hash and switch values
are often user or tenant identifiers.
