---
title: "Durable Authority"
---

The classic distributor accepts whole snapshots with client-chosen versions.
That's simple, but it has limits: two publishers can't safely edit at the same
time, anyone who can reach the port can publish, and the clustered profile
keeps its Raft votes in memory.

The **durable authority** is an opt-in profile of `detentd` that fixes all
three. Publishers open authenticated sessions and submit atomic, per-namespace
**transactions** with compare-and-set preconditions; the authority assigns
revisions and commits them through durable Raft consensus to local storage.

:::hint[Opt-in and still being qualified]{.caution}
The authority is complete as a component and has passed native, sanitizer and
three-platform qualification, but sustained fleet-scale qualification is still
open. It doesn't serve the legacy `/v1/snapshot` endpoint; agents receive its
data through [managed delivery](/Detent/guides/managed-delivery/). See the
[Roadmap](/Detent/roadmap/) for its exact status.
:::

---

## Build It

The authority is off by default:

```sh
cmake -B build -DDETENT_BUILD_AUTHORITY=ON
cmake --build build -j
```

It requires OpenSSL 3.0 or newer.

---

## Configure a Single Voter

A single-voter authority has local-disk durability without replicated
availability. Create a configuration file owned by the service account or root,
with mode `0600` (or a controlled group-readable `0640`):

```json title="authority.json"
{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "directory": "authority-data",
  "credentials": [{
    "token": "replace-with-a-random-token-at-least-32-characters",
    "producer": "publisher",
    "namespaces": ["payments"]
  }]
}
```

1. `cluster`, `group` and `epoch` are independent, nonzero 128-bit identities
   written as 32 lowercase hex digits. Generate fresh random ones; the values
   above are for illustration only.
2. `directory` is resolved relative to the configuration file. Its parent must
   already exist.
3. Each credential binds a bearer `token` (at least 32 characters) to a
   `producer` name and the `namespaces` it may write. Producer identity always
   comes from the credential, never from a request body.

```sh
chmod 600 authority.json
detentd --authority authority.json
```

It listens on `127.0.0.1:8317` by default; `--port` changes the port.
`--daemonize` and `--status` work as for the classic distributor.

---

## Publish a Transaction

Every request carries `Authorization: Bearer TOKEN` and the exact authority
identity. First, open a producer session:

```rest
POST /v2/open
Authorization: Bearer replace-with-a-random-token-at-least-32-characters

{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "expected_generation": 0,
  "nonce": "0123456789abcdef0123456789abcdef"
}
```

The response carries your session `generation`. Then submit transactions with
consecutive `sequence` numbers:

```rest
POST /v2/transactions
Authorization: Bearer replace-with-a-random-token-at-least-32-characters

{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "generation": 1,
  "sequence": 1,
  "namespace": "payments",
  "operations": [
    { "key": "payments:new_processor",
      "value": { "type": "bool", "default": false, "rollout_pct": 10 } },
    { "key": "payments:max_retries", "value": { "type": "int", "default": 3 } }
  ]
}
```

A transaction is atomic within its namespace: every operation applies, or none
does. An operation's `value` uses the ordinary [knob definition
schema](/Detent/reference/snapshot-format/), or `null` to delete.

### Compare-and-Set

To make sure you're not overwriting someone else's change, add a precondition:

1. `"expected_revision": N` on an operation succeeds only if that key's
   current modification revision is `N`.
2. `"expected_revision": 0` with `"absent_namespace_revision": R` succeeds only
   if the key is absent and the namespace is still at revision `R`.
3. `"expected_namespace": R` on the transaction checks the whole namespace's
   revision.

Without a precondition, an operation replaces unconditionally.

### Outcomes and Retries

| Outcome | Meaning | Next sequence? |
| --- | --- | --- |
| `applied` | Committed, with a new revision | Yes |
| `no_change` | Identical upsert or absent delete; nothing to commit | Yes |
| `conflict`, `capacity`, `exhausted` | Deterministic failure | Yes |
| `503` `outcome_unknown` | The outcome isn't known yet | **No**: retry the same generation, sequence and body |

A retried transaction with the same sequence and the same contents returns its
original outcome; with different contents, it's a conflict. Never advance the
sequence just because a connection dropped. Use `POST /v2/result` to look up
the outcome of a sequence, and `POST /v2/read` for strong reads of up to 128
keys.

The [Authority API](/Detent/reference/http-api/authority/) documents every
endpoint and field.

---

## Go Multi-Node

A replicated authority uses a fixed list of three or five voters, mutual TLS
between peers, and TLS for publishers:

```json title="authority.json (node 1 of 3)"
{
  "cluster": "…", "group": "…", "epoch": "…",
  "directory": "authority-data",
  "node": 1,
  "voters": [
    { "id": 1, "host": "10.0.0.1", "port": 9317, "certificate_sha256": "<64 hex digits>" },
    { "id": 2, "host": "10.0.0.2", "port": 9317, "certificate_sha256": "<64 hex digits>" },
    { "id": 3, "host": "10.0.0.3", "port": 9317, "certificate_sha256": "<64 hex digits>" }
  ],
  "peer_tls": { "certificate": "peer.pem", "key": "peer.key", "ca": "ca.pem" },
  "publisher_tls": { "certificate": "pub.pem", "key": "pub.key", "ca": "ca.pem" },
  "publisher_bind": "10.0.0.1",
  "publisher_name": "authority-1.internal",
  "credentials": [ … ]
}
```

Every voter shares the cluster, group, epoch and voter list; each sets its own
`node`. See [Operations: Durable Authority](/Detent/operations/authority/) for
certificates, pins, timing and the full field list.

---

## Migrate from a Classic Distributor

Import a legacy snapshot into a **new**, empty authority directory, with
publishers stopped until you've verified the result:

```sh
detent-authority-admin import authority.json legacy-snapshot.json
```

The tool validates the snapshot, applies it in namespace-scoped batches,
compares every resulting definition against the source, and writes a
checkpoint. A failed import leaves the directory for inspection; it never
resumes silently or overwrites an existing authority.

To roll back, stop the authority and export to a legacy snapshot with an
explicit version higher than the destination's current one:

```sh
detent-authority-admin export authority.json rollback-snapshot.json 1000
```
