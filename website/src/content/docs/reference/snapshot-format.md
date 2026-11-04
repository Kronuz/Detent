---
title: "Snapshot Format"
---

Knob-definition files, snapshots and patches all share one JSON schema,
`schema_version: 1`, parsed strictly by `detent/json_schema.hh` and
`detent/json_patch.hh` in the `detent::detent` target.

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
| `version` | yes | Positive 64-bit integer |
| `published_at` | no | RFC 3339 UTC timestamp, informational |
| `knobs` | yes | Object mapping [knob names](/Detent/reference/knobs/) to definitions |

---

## Knob-Definition File

A knob-definition file has the same shape **without** `version` and
`published_at`; including either field is a schema error. `parse_defaults`
reads it, and the test writer's `GenerationWriter::publish_json` publishes one
as a real generation (see [Testing](/Detent/guides/testing/)).

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

A patch uses the snapshot shape with two differences:

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
replaced or deleted as a unit.

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
7. More knobs than the caller's `max_knobs` (default 100,000).
8. A document larger than 16 MiB, or nested too deeply.

The parser returns an `UpdateError` naming the category; nothing is
published.

---

## Wire Formats Beyond JSON

The durable authority and managed agents use a narrow, versioned MessagePack
schema for snapshots, exact-base deltas and progress records, inside bounded
raw or zstd frames. It carries the same definitions plus per-namespace and
per-key revisions. It's an internal protocol between Detent components, not a
publishing format; publishers speak JSON. See
[Delivery and Local Images](/Detent/architecture/delivery/).
