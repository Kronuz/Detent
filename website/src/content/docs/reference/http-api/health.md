---
title: "Health API"
---

```rest
GET /v1/health
```

Both profiles answer `GET /v1/health` without authentication. It's meant for
load-balancer checks, dashboards and `--status`.

---

## Classic Distributor

```json
{
  "has_snapshot": true,
  "current_version": 2,
  "requests_served": 6,
  "puts_accepted": 2,
  "puts_rejected": 2,
  "uptime_seconds": 2,
  "clustered": false
}
```

| Field | Meaning |
| --- | --- |
| `has_snapshot` | A snapshot has been published or loaded from disk |
| `current_version` | The version currently served |
| `requests_served` | Requests handled since startup |
| `puts_accepted`, `puts_rejected` | Accepted and rejected `PUT`/`PATCH` publishes |
| `uptime_seconds` | Seconds since startup |
| `clustered` | Whether this node runs Raft |

In clustered mode the response adds:

| Field | Meaning |
| --- | --- |
| `is_leader` | Whether this node is the current Raft leader |
| `leader_address` | The leader's advertised `host:port` |

---

## Durable Authority

The authority publishes a cached health document about every 250 ms, and
immediately on readiness or fencing. Alongside its identity, role and
readiness, it reports bounded operational telemetry:

1. **Storage I/O.** Native versus fallback operation counts, and histograms
   such as `io_submitted_to_reaped_ns` and `io_job_settlement_ns`.
2. **Scheduling.** `owner_turn_ns` for each authority scheduling pass, and
   `wake_lateness_ns` for timer lateness.
3. **Replication.** `peer_decoded_to_admitted_ns` and append round-trip timing
   per authenticated connection.
4. **Publishers.** `publisher_admitted_to_ready_ns` and
   `reply_ready_to_retired_ns`.
5. **Freshness.** `snapshot_age_ms`, the age of the cached document.

Histograms expose a count, a saturating sum, a maximum, and buckets bounded by
`duration_bucket_upper_bounds_ns`. Fields are sampled independently, so one
response is approximate rather than a single atomic snapshot.

:::hint[Health document schema]{.construction}
The authority's health fields are still evolving with its qualification work,
and aren't yet a stable, versioned schema. Query a running authority for the
current set.
:::
