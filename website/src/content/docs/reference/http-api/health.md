---
title: "Health API"
---

```rest
GET /v1/health
```

The authority answers `GET /v1/health` without authentication. It's meant for
load-balancer checks, dashboards and `--status`. A request with a body is
rejected with `400` and `{"error":"health_body_forbidden"}`.

---

## The Health Document

The authority publishes a cached health document about every 250 ms, and
immediately on readiness or fencing. Its core fields:

| Field | Meaning |
| --- | --- |
| `mode` | `durable_single_voter` or `durable_fixed_voters` |
| `ready` | Whether the node reports ready |
| `role` | The node's consensus role, as a number |
| `term`, `committed`, `applied` | Consensus term, commit index and applied index |
| `revision` | The applied authority revision |
| `knobs` | Knobs in the applied state |
| `fenced` | Whether the node is fenced; after a runtime failure the cached document holds only `mode` and `fenced: true` |
| `snapshot_age_ms` | Age of the cached document when served |

Alongside these, it reports bounded operational telemetry:

1. **Storage I/O.** Native versus fallback operation counts, and histograms
   such as `io_submitted_to_reaped_ns` and `io_job_settlement_ns`.
2. **Scheduling.** `owner_turn_ns` for each authority scheduling pass, and
   `wake_lateness_ns` for timer lateness.
3. **Replication.** `peer_decoded_to_admitted_ns` and append round-trip timing
   per authenticated connection.
4. **Publishers.** `publisher_admitted_to_ready_ns` and
   `reply_ready_to_retired_ns`.

Histograms expose a count, a saturating sum, a maximum, and buckets bounded by
`duration_bucket_upper_bounds_ns`. Fields are sampled independently, so one
response is approximate rather than a single atomic snapshot.

:::hint[Health document schema]{.construction}
The authority's health fields are still evolving with its qualification work,
and aren't yet a stable, versioned schema. Query a running authority for the
current set.
:::
