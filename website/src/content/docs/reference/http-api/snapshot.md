---
title: "Snapshot API"
---

The classic distributor's whole data API is one resource, `/v1/snapshot`, with
three methods.

---

## Publish a Snapshot

```rest
PUT /v1/snapshot

{
  "schema_version": 1,
  "version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 }
  }
}
```

Replaces the entire snapshot. The body is a
[live snapshot](/Detent/reference/snapshot-format/#live-snapshot); its
`version` must be greater than the current version.

```json
{"accepted_version":1}
```

The snapshot is validated, persisted to disk, and becomes visible to the next
`GET` in that order. A knob missing from the body is removed.

---

## Update Some Knobs

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 2,
  "knobs": {
    "search:max_results": { "type": "int", "default": 100 },
    "checkout:old_flag": null
  }
}
```

Applies a [patch](/Detent/reference/snapshot-format/#patch): listed knobs are
replaced whole, `null` deletes, everything else is preserved. The patch is
merged under the distributor's lock, so two concurrent `PATCH`es can't both
merge onto the same base.

```json
{"accepted_version":2}
```

### Errors

| Status | Example body | Cause |
| --- | --- | --- |
| `400` | `{"error":"invalid knob name: bad name"}` | Invalid JSON or schema |
| `409` | `{"error":"stale or duplicate version"}` | `version` not above the current one |
| `413` | `{"error":"too many knobs"}` | Too many knobs, or body over 16 MiB |
| `500` | | Persisting the snapshot failed; the served state is unchanged |
| `503` | | Clustered write without a quorum decision in time |

In a cluster, `PUT` and `PATCH` responses include `X-Detent-Leader` naming the
current leader. Any node accepts writes.

:::hint[Versions are chosen by the publisher]{.caution}
The publisher picks each version, and the distributor only checks that it
increases. Two independent publishers can't safely share one distributor
without coordinating versions; the loser of a race gets `409` and must retry
with a fresh read. The [durable authority](/Detent/guides/durable-authority/)
assigns revisions server-side and adds compare-and-set instead.
:::

---

## Read the Snapshot

```rest
GET /v1/snapshot
```

```text
HTTP/1.1 200 OK
X-Snapshot-Version: 3
ETag: W/"38d11f3bf818730e"
Content-Type: application/json
```

```json
{"schema_version":1,"version":3,"knobs":{"payments:new_processor":{"type":"bool","default":false,"overrides":{"beta":{"rollout_pct":50},"employees":true},"rollout_pct":10},"search:max_results":{"type":"int","default":100}}}
```

Before the first publish, the response is `404 Not Found` with the body
`no snapshot published yet`.

### Conditional Requests

Send the last ETag in `If-None-Match`; an unchanged snapshot returns
`304 Not Modified` with no body:

```rest
GET /v1/snapshot
If-None-Match: W/"38d11f3bf818730e"
```

### Diffs

Send the version you already have in `X-Detent-Base-Version`, and the
distributor answers with only what changed since then:

```rest
GET /v1/snapshot
X-Detent-Base-Version: 2
```

```text
HTTP/1.1 200 OK
X-Snapshot-Version: 3
X-Patch-Base-Version: 2
ETag: W/"38d11f3bf818730e"
Content-Type: application/merge-patch+json
```

```json
{"schema_version":1,"version":3,"knobs":{"payments:new_processor":{"type":"bool","default":false,"overrides":{"beta":{"rollout_pct":50},"employees":true},"rollout_pct":10}}}
```

1. A base one version behind hits a precomputed diff with no per-request work.
2. A base up to 64 versions behind gets a diff composed from the retained
   per-step patches. It may include a harmless no-op entry for a knob that
   changed and changed back.
3. A missing, malformed, older or unknown base gets the full snapshot.

A diff response is always identified by `Content-Type:
application/merge-patch+json` and `X-Patch-Base-Version`; the client applies it
onto its own last accepted snapshot.

### Compression

```rest
GET /v1/snapshot
Accept-Encoding: zstd, gzip
```

Full bodies and diffs are compressed once per accepted update and cached.
`Content-Encoding` and `Vary: Accept-Encoding` are set on compressed responses.
