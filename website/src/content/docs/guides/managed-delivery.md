---
title: "Managed Delivery"
---

Managed delivery connects `detent-agent` to the
[durable authority](/Detent/guides/durable-authority/). Instead of polling a JSON
snapshot, the agent receives a compact binary snapshot or delta over
authenticated TLS, and publishes it as a durable, **immutable generation** on
local disk. Readers pin a generation and evaluate against it directly.

```d2 alt="The authority delivers authenticated snapshots and deltas to the managed agent, which publishes immutable generations to a local directory; detent get and SDK clients read the selected generation"
direction: right
authority: "detentd --authority\n(/v2/distribution)"
agent: "detent-agent --managed\n(private credential)"
dir: "generation directory\n(selected + immutable\ngeneration images)" {shape: cylinder}
readers: "detent get / SDK\n(public, read-only)"
authority -> agent: "TLS, bearer token,\nsnapshot or exact-base delta"
agent -> dir: "publish new generation,\nthen atomically select it"
readers -> dir: "pin + read"
```

Generations survive agent and host restarts, so a host can serve its last
known good configuration even before the agent reconnects.

---

## 1. Provision a Reader Credential

On the authority, add a credential for the agent: give it a `principal`, list
the namespaces it may read, and set `"publish": false` so a leaked agent token
can't write.
The scope identity that binds readers to this subscription is derived from the
principal, its policy epoch (default `1`) and the sorted namespaces.

---

## 2. Write the Private Agent Configuration

```json title="managed.json"
{
  "cluster": "63636363636363636363636363636363",
  "group": "67676767676767676767676767676767",
  "epoch": "65656565656565656565656565656565",
  "principal": "agent-reader",
  "namespaces": ["payments"],
  "token": "replace-with-the-authority-reader-token",
  "ca": "ca.pem"
}
```

Make it a regular, single-link file owned by root or the service account, with
mode `0600` or a controlled group-readable `0640`. Optional fields `address`,
`port` and `hostname` default to `127.0.0.1`, `8317` and `localhost`; the
address must be an IP literal. Certificate verification is always on.

---

## 3. Initialize and Run

Initialize an empty generation directory once:

```sh
detent-agent --managed managed.json --generation-dir ./agent-state --initialize-managed
```

Later starts omit `--initialize-managed` and require an existing store:

```sh
detent-agent --managed managed.json --generation-dir ./agent-state
```

`--upgrade-managed` explicitly adopts a compatible legacy generation directory.
Neither mode silently discards unreadable state.

---

## 4. Read

```sh
detent get payments:new_processor --hash user-42 --generation-dir ./agent-state
detent-agent --status --generation-dir ./agent-state
```

Reads work even while the agent is stopped: they read the selected durable
generation. Readers only need the public generation directory; they never open
the private credential file.

To make sure shell reads are bound to the intended authority scope, set all
four public identity fields in `agent.json`, or pass them as flags:

```json title="agent.json"
{
  "managed_path": "managed.json",
  "generation_dir": "agent-state",
  "generation_cluster": "63636363636363636363636363636363",
  "generation_group": "67676767676767676767676767676767",
  "generation_epoch": "65656565656565656565656565656565",
  "generation_scope": "<64 lowercase hex digits>"
}
```

A partial identity is rejected. Distribute this public identity separately from
the private token.

---

## 5. Read from C++

The C++ SDK can read the same generations with no network code at all. Link
`detent::local_client`:

```cpp
#include "detent/client.hh"
#include "detent_local/client_source.hh"

detent::InitOptions options;
options.defaults_path = "defaults.json";  // optional
options.read_source = detent::local::make_local_read_source(
    detent::local::ClientSourceOptions{.directory = generation_directory});
detent::Client client(std::move(options));

auto view = client.read_view();
auto maximum = view.get("search:max_results");
auto enabled = view.eval("checkout:new_path", "host-42", "beta");
```

Refresh work runs on one background thread; acquiring a view and looking up
knobs allocate nothing. CLI overrides still win, missing live keys still fall
back to baked defaults, and a failed refresh keeps the previous view.
Local-source clients use `read_view()`; `current_snapshot()` throws
`std::logic_error` because there's no eager snapshot to return.

---

## Resource Bounds

The managed agent accounts for its own memory, mappings and disk:

| Setting | Default |
| --- | --- |
| `heap_bytes` | 256 MiB managed heap |
| `mapping_bytes` | 512 MiB mapped virtual space |
| `disk_bytes` | 1 GiB named disk space |
| `disk_entries` | 4,096 entries |
| `disk_generations` | 1,024 generations |
| `poll_interval_ms` | 200 |
| `reconnect_maximum_ms` | 30,000, or the poll interval when larger |
| `timeout_ms` | 60,000 |
| `image_format` | `"flat-v1"`; opt in with `"paged-v2"` |
| `compaction_interval_ms` | 60,000; zero disables; enabled range 1,000–86,400,000 |
| `compression` | `true` |
| `artifact_mode` | `384` (`0600`) for users, `420` (`0644`) for system agents |

Subscription reconnects use independently seeded exponential equal jitter. Authenticated idle responses and durable publication reset backoff; successful long polls and disk waits retain their existing timing. `reconnect_maximum_ms` must cover `poll_interval_ms` and cannot exceed 86,400,000.

`artifact_mode` takes the decimal value `384`, `416` or `420` (octal `0600`,
`0640` or `0644`). Unknown or duplicate fields are rejected.

### Changing Authorities

Changing the authority identity requires an explicit `reset_from` object
naming the old `cluster`, `group`, `epoch` and `scope`. A trusted reset
publishes a new snapshot while keeping the local storage identity; an
unconfigured identity change fails startup. That's an administrative trust
decision, never an automatic fallback.

## Subscription Delivery

Add `"delivery_mode":"subscription"` to the private managed credential configuration to use publication windows and long polling. The default remains `"poll"`. Subscriptions reuse the same TLS verification, framed updates and durable local generations.

The agent opens from its recovered local cursor, receives one update and sends exact publication feedback only after local publication and recovery complete. An unchanged agent waits without repeatedly exporting authority state. Session expiry permits reopening from the actual durable cursor. Lost history requests an explicit new snapshot; changed identity or revoked authorization fences the agent until trusted configuration is corrected.

`poll_interval_ms` controls retry backoff in subscription mode. The authority negotiates the maximum long-poll wait, and each network operation remains bounded by `timeout_ms`. Shutdown drains accepted network work before a bounded best-effort session close. Subscriptions and relay fan-out have component acceptance. Opt-in incremental paged publication is under assembled-process qualification; sustained fleet capacity remains a separate gate.

## Paged local publication

Set `"image_format": "paged-v2"` in the private managed source configuration to publish incremental immutable pages. Relay bundle sources accept the same field. Existing configured flat heads migrate without changing the logical cursor; readers holding the old generation remain valid. New deltas replace changed pages and their index paths, while progress updates replace only the root.

Compaction runs between upstream exchanges and skips admission pressure. Disk limits must cover physical coexistence with pinned predecessors. A stopped agent's selected generation remains readable through the current SDK. Older flat-only binaries cannot read v2; switching the writer back to `flat-v1` requires a subsequent forward publication before the selected head becomes flat.

## Managed authority endpoint pools

A managed source can set `authority_endpoints` to an array of up to eight objects such as `[{"address":"127.0.0.1","port":8317},{"address":"127.0.0.2","port":8317}]`, replacing the top-level `address` and `port`. Destinations must be distinct IP addresses with nonzero ports. All entries share the configured TLS hostname, CA, credential, authority identity and namespace scope.

The coordinator tries members serially and stays on a successful member. Transport failures, unavailable authorities and follower read rejection permit rotation. Admission pressure and local storage failures retain the route. Authentication, authorization, TLS verification and unexpected identity or profile fail closed, including denials received while closing a session. Sessions and publication feedback remain bound to their opening endpoint.

For pooled subscriptions, `quorum_refresh_ms` defaults to 30000 and accepts 1000 through 86400000. Refresh uses bounded jitter of ±20%; idle responses do not postpone it. A fresh Open revalidates authority quorum after accepted local publication and recovery settle. Polling already performs quorum reads. Discovery time includes refresh, per-member request deadlines, retry backoff and accepted local work; the default 60-second request timeout provides no short failover guarantee.

Pools contain authority endpoints. Relay Open does not establish current authority quorum. An agent consuming a relay must use a single relay endpoint and explicit `"delivery_mode":"subscription"`.
