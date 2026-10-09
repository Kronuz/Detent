---
title: "Durable Authority"
---

Operating the opt-in durable authority: identities, credentials, multi-voter
TLS, storage and migration. For a walkthrough, start with the
[Durable Authority guide](/Detent/guides/durable-authority/).

---

## What You Get

| | Classic distributor | Durable authority |
| --- | --- | --- |
| Publish unit | Whole snapshot or patch | Atomic per-namespace transaction |
| Versions | Chosen by the publisher | Assigned by the authority |
| Concurrency control | Version must increase | Compare-and-set on keys and namespaces |
| Retries | Not idempotent | Exactly-once results per session sequence |
| Authentication | None | Bearer tokens, namespace authorization |
| Transport security | None | Publisher TLS; mutual TLS between voters |
| Consensus state | In memory | Durable terms, votes and log |
| Applying a change | Copies the whole map | Copies the affected tree paths only |
| Distribution | `GET /v1/snapshot` | Authenticated snapshots and deltas to managed agents |

---

## Configuration Reference

| Field | Required | Meaning |
| --- | --- | --- |
| `cluster`, `group`, `epoch` | yes | Independent nonzero 128-bit identities, 32 hex digits |
| `directory` | yes | Storage directory, relative to the config file |
| `credentials` | yes | Producer and reader credentials (below) |
| `node` | with `voters` | This process's voter ID |
| `voters` | multi-voter | 3 or 5 voters: `id`, IP `host`, `port`, `certificate_sha256` |
| `peer_tls` | multi-voter | `certificate`, `key`, `ca` for mutual TLS between voters |
| `publisher_tls` | multi-voter, or any non-loopback bind | `certificate`, `key`, `ca` for publisher HTTPS |
| `publisher_bind` | no | Publisher listen address, loopback by default |
| `publisher_name` | no | Certificate name `--status` verifies, `localhost` by default |
| `consensus_limits` | no | `log_entries` (64–65,536) and `log_bytes` (2–64 MiB) |
| `delivery_limits` | no | `heap_bytes` (default 128 MiB) and `wire_bytes` (default 256 MiB) |
| `storage_backend` | no | `auto`, or `native`/`fallback` on Linux |

Each credential:

| Field | Meaning |
| --- | --- |
| `token` | Bearer token, at least 32 characters, unique |
| `producer` | Unique producer name |
| `namespaces` | Namespaces this credential may write and read |
| `principal` | Optional stable principal for scope identity; defaults to the producer |
| `policy_epoch` | Optional nonzero policy epoch, default `1`; bump it when grants change |
| `publish` | Optional, default `true`; `false` makes a read-only agent credential |

Rotating a token while keeping the producer name preserves the producer's
session; changing the restore identity fences older requests.

---

## Multi-Voter Deployment

1. Generate fresh `cluster`, `group` and `epoch` identities and fresh, empty
   stores for every voter.
2. Issue each voter a leaf certificate and record the SHA-256 fingerprint of
   its DER encoding in every voter's `voters` list. A peer is admitted only if
   its certificate chains to `peer_tls.ca` **and** matches its pin.
3. Give every voter the same identities, voter list and limits; set `node` per
   process. A voter with a different quorum or application profile fails peer
   admission before any data is exchanged.
4. Configure `publisher_tls` for publishers; non-loopback publisher binds
   require it.

The replicated profile uses a 100 ms heartbeat, a 500 ms RPC deadline and
randomized 1–2 s elections. TLS below 1.2, early data and renegotiation are
disabled.

:::hint[Membership is fixed]{.caution}
Editing the voter list of an existing store is rejected at recovery. To change
membership, export the state, import it into a new group with a new restore
epoch, and switch writers.
:::

---

## Storage

The authority persists Raft terms, votes and log entries, and periodic
application checkpoints, to its directory. Checkpoints start automatically when
retained entries or bytes reach half their configured limits; old files are
reclaimed by bounded maintenance afterwards.

On macOS and FreeBSD the authority drives storage through a completion-based
backend; on Linux it uses io_uring when available, with a fallback.
`storage_backend` makes that choice explicit on Linux. Startup recovery is
synchronous and completes before requests are admitted.

:::hint[Durability scope]{.info}
Process-kill tests trace every acknowledged result across restarts. Power-loss
and filesystem-level qualification on specific hardware hasn't been done.
:::

---

## Service Management

Run `detentd --authority CONFIG` in the foreground under the same service
manager conventions as the classic distributor. Use a private state directory
owned by the service account, separate from any classic snapshot, and never
mix an authority with classic multicast peers.

```sh
detentd --status --authority /etc/detent/authority.json
```

With publisher TLS, `--status` checks the HTTPS endpoint with CA and name
verification and a two-second deadline.

---

## Migration

See [`detent-authority-admin`](/Detent/reference/cli/detent-authority-admin/)
for importing a classic snapshot into a new authority and exporting back for
rollback.
