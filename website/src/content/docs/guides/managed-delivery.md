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
| `timeout_ms` | 60,000 |
| `compression` | `true` |
| `artifact_mode` | `384` (`0600`) for users, `420` (`0644`) for system agents |

`artifact_mode` takes the decimal value `384`, `416` or `420` (octal `0600`,
`0640` or `0644`). Unknown or duplicate fields are rejected.

### Changing Authorities

Changing the authority identity requires an explicit `reset_from` object
naming the old `cluster`, `group`, `epoch` and `scope`. A trusted reset
publishes a new snapshot while keeping the local storage identity; an
unconfigured identity change fails startup. That's an administrative trust
decision, never an automatic fallback.
