---
title: "Authority API"
---

The [durable authority](/Detent/guides/durable-authority/) replaces whole-snapshot
publishing with authenticated producer sessions and atomic, per-namespace
transactions.

---

## Common Request Rules

Every `/v2` request:

1. Uses `POST` with a JSON body.
2. Carries `Authorization: Bearer TOKEN` for a configured credential.
3. Includes the exact authority identity: `cluster`, `group` and `epoch`, each
   32 lowercase hex digits.

Producer identity comes from the credential, never from the body. Duplicate
JSON fields, at any depth, are rejected before anything is proposed. Request
bodies are limited to 256 KiB. Authentication and body-admission failures close
the connection.

Credentials with `"publish": false` can read and receive distributions, but get
`403` from `/v2/open`, `/v2/close` and `/v2/transactions`.

---

## `POST /v2/open`

Opens (or reopens) the producer's session.

```rest
POST /v2/open
Authorization: Bearer TOKEN

{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "expected_generation": 0,
  "nonce": "0123456789abcdef0123456789abcdef"
}
```

| Field | Meaning |
| --- | --- |
| `expected_generation` | The producer's previous session generation (`0` the first time) |
| `nonce` | Nonzero, 32 hex digits; binds retries of this open |

A successful open fences every older generation of this producer.

## `POST /v2/close`

```json
{ "cluster": "…", "group": "…", "epoch": "…", "generation": 1 }
```

Fences the current generation. The producer's retained results stay in the
bounded registry.

---

## `POST /v2/transactions`

```rest
POST /v2/transactions
Authorization: Bearer TOKEN

{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "generation": 1,
  "sequence": 1,
  "namespace": "payments",
  "expected_namespace": 4,
  "operations": [
    { "key": "payments:new_processor",
      "value": { "type": "bool", "default": false, "rollout_pct": 10 },
      "expected_revision": 3 },
    { "key": "payments:max_retries",
      "value": { "type": "int", "default": 3 },
      "expected_revision": 0, "absent_namespace_revision": 4 },
    { "key": "payments:legacy_flag", "value": null }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `generation` | The open session's generation |
| `sequence` | Contiguous per-session sequence number, starting at 1 |
| `namespace` | The knobset every key must belong to |
| `expected_namespace` | Optional: require this namespace revision |
| `operations[].key` | A knob name in `namespace` |
| `operations[].value` | A [knob definition](/Detent/reference/knobs/#definitions), or `null` to delete |
| `operations[].expected_revision` | Optional: the key's current modification revision, or `0` for "absent" |
| `operations[].absent_namespace_revision` | With `expected_revision: 0`: the namespace revision the absence was observed at |

A transaction holds up to 128 operations and is atomic within its namespace.
Preconditions are evaluated against the state before the transaction. Checking
absence against a namespace revision detects delete-and-recreate races without
keeping a tombstone for every deleted key.

### Outcomes

| Outcome | Meaning | Consumes the sequence |
| --- | --- | --- |
| `applied` | Committed with a new revision | yes |
| `no_change` | Identical upsert or absent delete | yes |
| `conflict` | A precondition failed, or a retry changed its contents | yes |
| `capacity` | A logical limit would be exceeded | yes |
| `exhausted` | Revision space exhausted | yes |
| `result_unavailable` | The retained result for this sequence was evicted; it never executes again | n/a |
| `unknown_session`, `session_fenced`, `out_of_order` | Wrong or fenced generation, or a sequence gap | no |
| `invalid`, `identity_conflict` | Malformed request, or wrong authority identity | no |

If the outcome can't be determined before the deadline, the request fails with
`503` and `{"error":"outcome_unknown"}`: the transaction may or may not have
committed. Retry the
**same** generation, sequence and body until you get a terminal outcome. A
retryable overload rejection before admission consumes nothing.

Responses carry the authority identity, the outcome and the resulting
revision. The last 256 results of the current generation are retained: a retry
with identical contents returns the original outcome, a retry with different
contents is a conflict, and a sequence whose result was evicted returns
`ResultUnavailable` and never executes again.

---

## `POST /v2/read`

```json
{ "cluster": "…", "group": "…", "epoch": "…",
  "namespace": "payments", "keys": ["payments:new_processor", "payments:max_retries"] }
```

A strong read of up to 128 unique keys, behind a quorum barrier. The reply
carries the authority identity, the `namespace_revision`, the selected `knobs`
and their `key_revisions`.
Oversized selections return `reply_bound`.

## `POST /v2/result`

```json
{ "cluster": "…", "group": "…", "epoch": "…", "generation": 1, "sequence": 7 }
```

Looks up the retained outcome of one of the producer's own transactions,
behind a quorum barrier.

---

## `POST /v2/distribution`

Used by [managed agents](/Detent/guides/managed-delivery/), not by publishers.

```json
{
  "cluster": "…", "group": "…", "epoch": "…",
  "scope": "<64 hex digits>",
  "namespaces": ["payments"],
  "profile": 1,
  "coding": "zstd",
  "base": { "cluster": "…", "group": "…", "epoch": "…", "scope": "…", "revision": 41 }
}
```

The response has type `application/vnd.detent.distribution` and carries
`X-Detent-Profile: 1`, `X-Detent-Coding`, `X-Detent-Kind` (`1` snapshot, `2`
delta, `3` progress) and `X-Detent-Revision`. The body is a sequence of bounded
distribution frames. A delta or progress record is only sent when the retained
history exactly covers the requested `base`; otherwise the authority sends a
snapshot.

---

## Limits

| Limit | Value |
| --- | --- |
| Live knobs | 500,000 |
| Namespaces | 4,096 |
| Producers | 1,024 |
| Operations per transaction | 128 |
| Canonical request size | 256 KiB |
| Definition size | 16 KiB |
| Overrides per definition | 256 |
| Admitted requests per producer | 16 |
| Outstanding transactions per producer | 1 |
| Request slots | 1,024 |
| Aggregate body reservation | 16 MiB |
| Producers grouped into one Raft entry | 64, within 5 ms |
