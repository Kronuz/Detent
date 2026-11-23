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
authority: "detent-authority --authority\n(/v2/distribution)"
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

A start never silently discards unreadable state.

---

## 4. Read

```sh
detent get payments:new_processor --hash user-42 --generation-dir ./agent-state
detent-agent --status --generation-dir ./agent-state
```

Reads work even while the agent is stopped: they read the selected durable
generation. Readers only need the public generation directory; they never open
the private credential file.

A shell read attaches to its knob's namespace entry (`ns` for `ns:feature`).

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
auto enabled = view.get_as<bool>("checkout:new_path", "host-42", "beta");
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
| `retry_interval_ms` | 200 |
| `reconnect_maximum_ms` | 30,000, or the retry interval when larger |
| `timeout_ms` | 60,000 |
| `compaction_interval_ms` | 60,000; zero disables; enabled range 1,000–86,400,000 |
| `compression` | `true` |
| `artifact_mode` | `384` (`0600`) for users, `420` (`0644`) for system agents |

Session reconnects use independently seeded exponential equal jitter. Authenticated idle responses and durable publication reset backoff; successful long polls and disk waits retain their existing timing. `reconnect_maximum_ms` must cover `retry_interval_ms` and cannot exceed 86,400,000.

`artifact_mode` takes the decimal value `384`, `416` or `420` (octal `0600`,
`0640` or `0644`). Unknown or duplicate fields are rejected.

### Changing Authorities

A new cluster or a new incarnation of a namespace arrives as a full copy of
that namespace; no configured identity transition exists.

## Session Delivery

The agent follows the configured `namespaces` (every granted namespace when the list is absent) over one W5 session: it opens with each namespace's durable cursor, and each `next` long-polls for the frames past them. A response's frames publish as one local cycle before the next request. Session expiry reopens from the durable cursors. Lost history answers a full copy; changed identity or revoked authorization fences the agent until trusted configuration is corrected.

`retry_interval_ms` is the first retry after a failure. The authority negotiates the maximum long-poll wait, and each network operation remains bounded by `timeout_ms`. Shutdown drains accepted network work before a bounded best-effort session close.

## Paged local publication

The agent and relay bundle sources publish incremental immutable pages. New deltas replace changed pages and their index paths, while progress updates replace only the root; readers holding an older generation remain valid.

Compaction runs between upstream exchanges and skips admission pressure. Disk limits must cover physical coexistence with pinned predecessors. A stopped agent's selected generation remains readable through the current SDK. A store written in an earlier layout is refused once and the agent bootstraps a fresh copy.

## Managed authority endpoint pools

A managed source can set `authority_endpoints` to an array of up to eight objects such as `[{"address":"127.0.0.1","port":8317},{"address":"127.0.0.2","port":8317}]`, replacing the top-level `address` and `port`. Destinations must be distinct IP addresses with nonzero ports. All entries share the configured TLS hostname, CA, credential, authority identity and namespace scope.

The coordinator tries members serially and stays on a successful member. Transport failures, unavailable authorities and follower read rejection permit rotation. Admission pressure and local storage failures retain the route. Authentication, authorization, TLS verification and unexpected identity or profile fail closed, including denials received while closing a session. Sessions and publication feedback remain bound to their opening endpoint.

For pooled sessions, `quorum_refresh_ms` defaults to 30000 and accepts 1000 through 86400000. Refresh uses bounded jitter of ±20%; idle responses do not postpone it. A fresh Open revalidates authority quorum after accepted local publication and recovery settle. Discovery time includes refresh, per-member request deadlines, retry backoff and accepted local work; the default 60-second request timeout provides no short failover guarantee.

Pools contain authority endpoints. Relay Open does not establish current authority quorum. An agent consuming a relay uses a single relay endpoint.
