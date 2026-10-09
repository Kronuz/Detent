---
title: "Snapshot Format"
---

Defaults files, live snapshots, `PUT` bodies, `PATCH` bodies and diff responses
all share one JSON schema, `schema_version: 1`, parsed strictly.

---

## Live Snapshot

```json
{
  "schema_version": 1,
  "version": 42,
  "published_at": "2026-10-09T09:16:59Z",
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 },
    "payments:new_processor": {
      "type": "bool",
      "default": false,
      "rollout_pct": 10,
      "overrides": { "employees": true, "beta": { "rollout_pct": 50 } }
    }
  }
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `schema_version` | yes | Exactly `1` |
| `version` | yes | Positive 64-bit integer; must exceed the receiver's accepted version |
| `published_at` | no | RFC 3339 UTC timestamp, informational |
| `knobs` | yes | Object mapping [knob names](/Detent/reference/knobs/) to definitions |

---

## Defaults File

A baked defaults file has the same shape **without** `version` and
`published_at`. It is always version `0`; including either field is a schema
error.

```json
{
  "schema_version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false }
  }
}
```

---

## Patch

A `PATCH /v1/snapshot` body, and a diff returned by the distributor, use the
snapshot shape with two differences:

1. It contains only the knobs that changed.
2. A knob mapped to `null` is deleted from the live snapshot.

```json
{
  "schema_version": 1,
  "version": 43,
  "knobs": {
    "search:max_results": { "type": "int", "default": 50 },
    "checkout:old_flag": null
  }
}
```

This follows JSON Merge Patch (RFC 7396) at knob granularity: each knob is
replaced or deleted as a unit. A client always applies a patch onto **its own**
last accepted live snapshot, never onto whatever base the sender claims, so a
confused sender can at worst cause a rejected update, never a wrong merge.

Deleting a knob that also has a baked default makes readers fall back to the
baked definition.

---

## Strictness

The parser rejects the whole document, never part of it, when it finds:

1. Invalid JSON or invalid UTF-8.
2. A missing required field, or a field of the wrong JSON type.
3. Any unknown field, at the top level or inside a definition.
4. A duplicate key: top-level field, knob name or group name.
5. An invalid knob name or group name.
6. `rollout_pct` outside `0`–`100`, or a group override that is neither a
   boolean nor `{"rollout_pct": N}`.
7. More knobs than the receiver's `max_knobs` (default 100,000).
8. A document larger than 16 MiB, or nested too deeply.

A rejected update is counted and logged by category, and the receiver keeps
its last known good snapshot.

---

## Wire Formats Beyond JSON

The durable authority and managed agents use a narrow, versioned MessagePack
schema for snapshots, exact-base deltas and progress records, inside bounded
raw or zstd frames. It carries the same definitions plus per-namespace and
per-key revisions. It's an internal protocol between Detent components, not a
publishing format; publishers still speak JSON. See
[Delivery and Local Images](/Detent/architecture/delivery/).
