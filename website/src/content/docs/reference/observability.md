---
title: "Observability"
---

Detent exposes what you need to answer two questions: *is configuration
fresh?* and *are reads succeeding?*

---

## Reader Diagnostics

A `detent::Reader` keeps no counters and exports nothing; two calls answer both
questions:

```cpp
auto revision = reader.revision("checkout");   // std::optional<std::uint64_t>
auto result = reader.lookup("checkout:new_path");
if (!result) log(detent_status_name(result.status));
```

1. **Freshness.** `revision(namespace)` is the revision of the namespace's
   attached generation, or empty when none is attached yet. Compare it with
   the revision the authority committed.
2. **Read health.** `lookup(name).status` says why a read returned its
   fallback: `MISSING`, `WRONG_TYPE`, `UNAVAILABLE`, `SOURCE_FAILURE` and the
   rest in [Errors and Failures](/Detent/reference/errors/#lookup-statuses).

Python returns `revision()` as an `int` or `None` and `lookup()` as a `Lookup`
with `.status` and `.value`; Rust returns `Option<u64>` and
`Result<Value, Status>`.

## Suggested Alerts

1. **Staleness.** A namespace's `revision` behind the authority's committed
   revision for longer than your kill switches can tolerate.
2. **Unavailable reads.** `UNAVAILABLE` or `SOURCE_FAILURE` from `lookup` after
   startup: the host's agent has no usable generation.
3. **Missing reads.** `MISSING` for a name your code reads: a typo, or a knob
   that was never published.

Knob names, hash values, switch values and knob values belong in none of these
signals' labels, so cardinality stays bounded and user identifiers never leak
into monitoring.

:::hint[Metrics exporter]{.unimplemented}
There's no Prometheus or OpenMetrics endpoint on the daemons, and no exporter
for applications. Read `revision()` and `lookup()` statuses and publish them
through your own metrics library.
:::

---

## Daemon Health

1. `GET /v1/health` on the [authority](/Detent/reference/http-api/health/).
2. `detentd --status` and `detent-agent --status` from the command line.
3. Daemon logs, described in [Logging](/Detent/operations/logging/).

## What's Never Logged

Hash values, switch values and knob values are never logged, and rejected
publishes are logged by category without their payload. Hash and switch values
are often user or tenant identifiers.
