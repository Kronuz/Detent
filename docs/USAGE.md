# Usage guide

Detailed examples for four deployment shapes:

1. One process reading baked defaults only.
2. One `detentd` distributor serving live updates.
3. One `detent-agent` daemon serving host-local non-C++ readers.
4. A three-node `detentd` cluster using Raft consensus.

Build once before any scenario:

```sh
cmake -B build
cmake --build build -j
```

That produces `build/src/detentd`, `build/src/detent-agent`, `build/src/detent`, the static libraries, the examples, the bindings targets, and the tests.

---

## Scenario 1: static config, no network

Use this when a process only needs baked defaults and only picks up changes on restart.

`defaults.json`:

```json
{
  "schema_version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 }
  }
}
```

Minimal process:

```cpp
#include "detent/client.hh"
#include <cstdio>

int main() {
    detent::InitOptions opts;
    opts.defaults_path = "defaults.json";
    detent::Client client(std::move(opts));

    std::printf("search:max_results = %lld\n",
                (long long)client.get_or_panic("search:max_results"));
    std::printf("checkout:killswitch_new_path = %s\n",
                client.eval_or_panic("checkout:killswitch_new_path")
                    ? "true"
                    : "false");
}
```

Expected output:

```text
search:max_results = 25
checkout:killswitch_new_path = false
```

With no `source_path` and no `source_poll`, the client does one synchronous startup parse and then serves reads from the published in-memory snapshot.

### Scoped reads and managed generation sources

`Client::read_view()` pins one immutable evaluation view. Use it when several reads must observe the same local generation; it remains valid across refreshes and after client destruction. Existing file/default clients support this API too.

The managed reader library can consume an already provisioned immutable-generation directory. Link `detent::local_client` and configure its worker refresh:

```cpp
#include "detent/client.hh"
#include "detent_local/client_source.hh"

detent::InitOptions options;
// Optional: options.defaults_path = "defaults.json";
options.read_source = detent::local::make_local_read_source(
    detent::local::ClientSourceOptions{.directory = generation_directory});
detent::Client client(std::move(options));
auto view = client.read_view();
auto maximum = view.get("search:max_results");
auto enabled = view.eval("checkout:new_path", "host-42", "beta");
```

The source performs filesystem work on one configuration thread; successful scoped acquisition and lookup need no merged snapshot allocation. CLI overrides win over live definitions, and missing live keys fall back to baked defaults. Type and conversion errors do not fall back. Failed refresh preserves the prior view, and `client.health()` reports source connectivity. Local-source clients use `read_view()` instead of `current_snapshot()`, which rejects eager conversion with `logic_error`. Scoped reads do not increment their parent client's metrics. Destroy automatically refreshing sources outside an event-loop turn because destruction joins their worker.

The opt-in managed agent publishes immutable generations for this SDK source. Its configuration and startup modes are described below; the complete managed-agent/SDK component has Architect acceptance and platform/sanitizer qualification. See the [delivery integration blueprint](DELIVERY_INTEGRATION_ARCHITECTURE.md) for current implementation and acceptance gates.

### Gradual rollouts and per-group overrides

Bool knobs can combine a rollout percentage with named overrides. Overrides accept a boolean or `{"rollout_pct": 50}` for a group-specific rollout. The group percentage uses the same hash bucket; without a hash it uses the knob default. Unknown groups fall through to the knob-wide rollout:

```json
{
  "schema_version": 1,
  "knobs": {
    "payments:new_processor": {
      "type": "bool",
      "default": false,
      "rollout_pct": 10,
      "overrides": { "internal_employees": true }
    }
  }
}
```

```cpp
client.eval_or_panic("payments:new_processor");                               // false
client.eval_or_panic("payments:new_processor", "user-1");                     // false
client.eval_or_panic("payments:new_processor", "user-42");                    // true
client.eval_or_panic("payments:new_processor", "user-2", "internal_employees"); // true
```

Evaluation order is:

1. CLI override.
2. Exact-match `switch_val` override.
3. `rollout_pct` gate, when `hash_val` is present.
4. Baked or live default.

The rollout bucket is stable for the same `(knob_name, hash_val)` pair across processes and restarts.

Save the rollout JSON above as `defaults.json` before these shell reads. `detent get` starts the local daemon automatically and exposes the same `--hash` and `--switch` parameters:

```sh
./build/src/detent get payments:new_processor --defaults defaults.json
./build/src/detent get payments:new_processor --hash user-42
./build/src/detent get payments:new_processor --hash user-2 --switch internal_employees
```

---

## Scenario 2: one distributor, live updates

Use this when several processes need live updates without a restart.

Start one distributor:

```sh
./build/src/detentd &
```

No arguments are needed: `detentd` listens on port `8317`. It creates its state directory automatically and writes `snapshot.json` on the first accepted publish. With no saved snapshot, `GET /v1/snapshot` returns `404`.

| Platform | Default state directory |
| --- | --- |
| Linux / FreeBSD | `$XDG_STATE_HOME/detent`, otherwise `~/.local/state/detent` |
| macOS | `~/Library/Application Support/Detent/state` |

An absolute `XDG_STATE_HOME` overrides the native state location on macOS too. Snapshot storage is durable state, separate from the agent's temporary runtime files. No defaults file is required by the distributor.

For persistent distributor settings, put `detentd.json` in the same user or system config directory as `agent.json` (see [agent configuration](#persistent-agent-configuration)):

```json
{
  "port": 8317,
  "persist_path": "state/snapshot.json",
  "reactors": 4,
  "workers": 8
}
```

All fields are optional. A relative `persist_path` is resolved against the config file's directory; omitting it uses the platform state directory above. `--config PATH` selects a file explicitly. User config takes precedence over system config. Flags override `DETENTD_PORT`, `DETENTD_PERSIST_PATH`, `DETENTD_REACTORS`, and `DETENTD_WORKERS`, which override config values.

Explicit starts are still supported:

```sh
./build/src/detentd --port 8317 --persist-path ./state/snapshot.json
# Legacy positional syntax:
./build/src/detentd 8317 ./state/snapshot.json
```

`detentd` runs in the foreground; the `&` above backgrounds it through the shell. Cluster membership and Raft timing still use explicit CLI flags.

Publish the first snapshot:

```sh
curl -s -X PUT localhost:8317/v1/snapshot -d '{
  "schema_version": 1,
  "version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 }
  }
}'
```

Response:

```json
{"accepted_version":1}
```

Read it back directly:

```sh
curl -s localhost:8317/v1/snapshot
```

Point a client at it:

```cpp
#include "detent/client.hh"
#include "detent_http/http_source.hh"

using namespace std::chrono_literals;

detent::InitOptions client_opts;
client_opts.defaults_path = "defaults.json";
client_opts.source_poll =
    detent_http::make_http_poll_source("127.0.0.1", 8317, "/v1/snapshot", 2s);
client_opts.poll_interval = 200ms;
detent::Client client(std::move(client_opts));
```

`detentd` serves `GET /v1/snapshot` with a cached weak ETag. Pollers send `If-None-Match`; unchanged state returns `304 Not Modified`.

Inspect the conditional GET behavior:

```sh
curl -sv localhost:8317/v1/snapshot 2>&1 | grep -i '< HTTP\|etag'
etag=$(curl -sD - -o /dev/null localhost:8317/v1/snapshot | awk '/^ETag:/ {print $2}' | tr -d '\r')
curl -sv localhost:8317/v1/snapshot -H "If-None-Match: $etag" 2>&1 | grep -i '< HTTP\|etag'
```

Health endpoint:

```sh
curl -s localhost:8317/v1/health
```

The JSON includes:

- `has_snapshot`
- `current_version`
- `requests_served`
- `puts_accepted`
- `puts_rejected`
- `uptime_seconds`

Update one knob with a higher version:

```sh
curl -s -X PUT localhost:8317/v1/snapshot -d '{"schema_version":1,"version":2,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":false},"search:max_results":{"type":"int","default":500}}}'
```

Every polling client converges to version 2 within one `poll_interval`.

### Ramp a feature from 10% to 100%

With the distributor above running, stop any defaults-only agent from Scenario 1:

```sh
kill -TERM "$(./build/src/detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

Publish a 10% rollout at version 3. PATCH preserves the other knobs:

```sh
curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot -d '{
  "schema_version": 1,
  "version": 3,
  "knobs": {
    "payments:new_processor": {
      "type": "bool",
      "default": false,
      "rollout_pct": 10,
      "overrides": { "internal_employees": true }
    }
  }
}'

./build/src/detent get payments:new_processor --hash user-1 --default false
# false
./build/src/detent get payments:new_processor --hash user-42 --default false
# true
./build/src/detent get payments:new_processor --hash user-2 --switch internal_employees
# true
```

Use a stable user or account ID as `--hash`. Repeated reads keep the same assignment; increasing the percentage keeps previously included users enabled. The percentage describes hash buckets, so a small population need not split exactly 10/90. An exact group override wins over the rollout gate.

Ramp to 50%, then 100%, using a higher version for each publish:

```sh
for step in 4:50 5:100; do
  version=${step%:*}
  percentage=${step#*:}
  curl --fail-with-body -sS -X PATCH localhost:8317/v1/snapshot -d "{
    \"schema_version\": 1, \"version\": $version,
    \"knobs\": {\"payments:new_processor\": {
      \"type\": \"bool\", \"default\": false, \"rollout_pct\": $percentage,
      \"overrides\": {\"internal_employees\": true}
    }}
  }"
done
```

Allow at least one agent poll interval after each update (200 ms by default). At 100%, every read with `--hash` returns `true`; reads without a hash still use `default`, which remains `false`. To finish the rollout for all callers, publish `{"type":"bool","default":true}` as the complete knob definition at the next version. To roll back everyone, publish `{"type":"bool","default":false}` instead; omitting the rollout and overrides removes both. PATCH replaces each supplied knob definition as a whole.

`--default false` is a fallback for that individual read when the knob cannot be read. It does not change the published knob or configure the daemon. `--defaults PATH` supplies an optional file of typed defaults to a newly started agent. Neither is required for this example.

The commands above assume Scenario 2 started at version 1. If reusing an existing distributor, choose versions greater than its current version.

### Partial updates (PATCH), diffs, and compression

Update just the changed knob:

```sh
curl -s -X PATCH localhost:8317/v1/snapshot -d '{"schema_version":1,"version":6,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}}'
```

Pollers can ask for the cached diff from their last accepted version:

```sh
curl -s -D - localhost:8317/v1/snapshot -H "X-Detent-Base-Version: 5"
```

When the base version matches, `detentd` replies with:

- `Content-Type: application/merge-patch+json`
- `X-Snapshot-Version: 6`
- `X-Patch-Base-Version: 5`

If the base version is missing, stale, or unknown, `detentd` falls back to the full snapshot body.

Compression is negotiated with `Accept-Encoding` and cached per accepted update. Bodies smaller than 256 bytes stay uncompressed.

---

## Scenario 3: host-local reads for non-C++ callers

Use this when local shell scripts or non-C++ programs need fast reads without each one polling `detentd`.

With Scenario 2's distributor running on `127.0.0.1:8317` and a snapshot published, read a knob directly:

```sh
# Stop any defaults-only agent left over from Scenario 1.
kill -TERM "$(./build/src/detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
./build/src/detent get search:max_results
# 500 (or the latest published value)
./build/src/detent-agent --status
kill -TERM "$(./build/src/detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

`get` auto-starts the local agent. No configuration file, defaults file, or environment variables are required for the local distributor. The agent connects to `127.0.0.1:8317` and waits up to two seconds for its first snapshot. If the source is unavailable, the daemon stays running and keeps trying; `get` reports that no snapshot has arrived. `--default VALUE` supplies a fallback for that read.

### Persistent agent configuration

For a different distributor, write `agent.json` in the user config directory:

| Platform | User config directory | System fallback |
| --- | --- | --- |
| Linux | `$XDG_CONFIG_HOME/detent`, otherwise `~/.config/detent` | `/etc/detent` |
| FreeBSD | `$XDG_CONFIG_HOME/detent`, otherwise `~/.config/detent` | `/usr/local/etc/detent` |
| macOS | `~/Library/Application Support/Detent` | `/Library/Application Support/Detent` |

An absolute `XDG_CONFIG_HOME` overrides the native user location on macOS too. The first existing `agent.json` wins (user before system); `--config PATH` selects a file explicitly. Config files contain JSON:

```json
{
  "source_address": "127.0.0.1:8317",
  "poll_interval_ms": 200
}
```

Then use plain `detent get NAME` from any directory. `source_address` is a `host:port` address; the transport is HTTP and the endpoint is `/v1/snapshot`. `source_path` selects a local live snapshot file instead. These source settings are mutually exclusive.

### Optional local defaults and runtime files

A live snapshot already contains knob types and values. `defaults.json` is optional and supplies local fallback knobs before the first source update or when a live snapshot omits a knob. Place it beside `agent.json`, or set `"defaults_path": "defaults.json"` in the config. Relative paths in config are resolved against the config directory. An explicitly configured defaults file must exist and contain valid defaults JSON; an absent conventional file is simply skipped. The agent does not create an empty defaults file.

For local-only reads, use `--defaults PATH` or place `defaults.json` in the standard config directory. With defaults present and no source configured, the agent serves local defaults without attempting a network connection. To combine defaults with live updates, configure `source_address` as well.

Runtime files (`agent.pid`, `agent.log`, `snapshot.shm`) go in `$XDG_RUNTIME_DIR/detent` when an absolute XDG runtime directory is set. Otherwise the agent uses a private `detent-<uid>` directory under the system temporary directory (`TMPDIR` when set, normally `/tmp` on Linux/FreeBSD and a per-user temporary directory on macOS). It creates the runtime directory with mode `0700`. `runtime_dir` in config or `--runtime-dir PATH` overrides it.

Flags override environment variables, which override config. The optional variables are `DETENT_SOURCE_ADDRESS`, `DETENT_SOURCE_PATH`, `DETENT_DEFAULTS_PATH`, `DETENT_POLL_INTERVAL_MS`, and `DETENT_RUNTIME_DIR`. `--source-http`, `DETENT_SOURCE_HTTP`, and config's `source_http` remain aliases for the address setting. A source flag replaces the configured source as a whole. Startup settings configure a new daemon; stop the running daemon to apply changed settings. Keep the same runtime directory for `get`, `status`, and `stop`.

Startup errors are reported to the caller. Daemon output is saved in `agent.log` in the runtime directory.

Implementation summary:

- `detent-agent` runs one real `detent::Client`.
- On each accepted update, it serializes the snapshot into a flat shared-memory layout.
- `get` maps that file read-only and evaluates directly against the published bytes.
- The read path does not talk to the daemon process per request.

Measured throughput on the same development machine:

- `detent_read_bench 4 2000000`: 6.71M ops/sec
- `detent_agent_shm_bench 4 2000000`: 36.2M ops/sec

---

## Scenario 4: a multi-node cluster

Use this when the distributor itself needs replication and failover.

Start three same-host replicas with the exact `--cluster` syntax from `src/detentd/main.cc`:

```sh
mkdir -p .scratch
./build/src/detentd 8081 .scratch/cluster-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 &
./build/src/detentd 8082 .scratch/cluster-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083 &
./build/src/detentd 8083 .scratch/cluster-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082 &
```

Each node's `--cluster` arguments are:

1. Cluster token.
2. Multicast port.
3. Multicast group.
4. Multicast interface.
5. This node's own advertised `host:port`.
6. Every peer's advertised `host:port`.

Inspect cluster state:

```sh
curl -s localhost:8081/v1/health
curl -s localhost:8082/v1/health
curl -s localhost:8083/v1/health
```

In clustered mode, the health response adds:

- `clustered`
- `is_leader`
- `leader_address`

Exactly one node reports `is_leader: true` after election converges.

Publish through any node:

```sh
curl -s -D - -X PUT localhost:8082/v1/snapshot -d '{
  "schema_version": 1,
  "version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 }
  }
}'
```

The response still carries `{"accepted_version":1}`. In clustered mode it also includes `X-Detent-Leader`, which names the current leader.

Read the same snapshot back from another node:

```sh
curl -s localhost:8083/v1/snapshot
```

Update a single knob through a different node:

```sh
curl -s -X PATCH localhost:8081/v1/snapshot -d '{"schema_version":1,"version":2,"knobs":{"search:max_results":{"type":"int","default":500}}}'
```

Read it from any node:

```sh
curl -s localhost:8082/v1/snapshot
```

A real `detent::Client` in this setup should use the multi-node `make_http_poll_source` overload rather than pinning itself to one node:

```cpp
opts.source_poll = detent_http::make_http_poll_source({
    {"127.0.0.1", 8081}, {"127.0.0.1", 8082}, {"127.0.0.1", 8083},
});
```

It starts on a random one of the three (so a fleet of client processes spreads across the cluster instead of piling onto node one) and fails over to another if its current node stops answering -- reads don't need the leader specifically, every node applies the identical committed sequence.

Current automated cluster coverage in `tests/detentd/test_raft_cluster.cc` exercises:

- leader election,
- writes submitted through a follower,
- PATCH replication,
- leader failover with a surviving majority,
- restart safety and rejoin,
- dead-follower liveness (a permanently-gone follower doesn't block the surviving majority),
- a node's own write right after restarting isn't confused with one of its pre-crash writes.

`tests/e2e/` complements this with a slower harness that spawns real `detentd` subprocesses and kills them with real signals -- run it after any change to `src/detentd/` or the vendored Raft wiring; see `tests/e2e/README.md`.

`src/detentd/CLUSTERING.md` is the full clustering design reference.

## Cleaning up

```sh
kill -TERM "$(./build/src/detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
kill %1 %2 %3
```

## Where to go next

- [`README.md`](../README.md): architecture, dependencies, bindings, benchmarks, test coverage, and production-readiness notes.
- [`justknobs-implementation-spec.md`](../justknobs-implementation-spec.md): the source specification.
- `src/detentd/CLUSTERING.md`: the authoritative clustering design.

System-wide deployment: see [System services](SERVICES.md) for service accounts, directory ownership, supervision, and logging on Linux, FreeBSD, and macOS.

The shell reader is `detent get NAME`. Both daemons run directly in the foreground, accept `--daemonize` for user background operation, and provide read-only `--status`. Send SIGTERM to stop an unmanaged daemon; supervised services use their service manager. The reader launches a sibling `detent-agent` binary, or finds it on `PATH` when invoked by name.

## Durable authority profile

The opt-in `detentd --authority CONFIG` profile uses a separate durable store and loopback transaction API. It preserves foreground execution, `--daemonize` and read-only `--status`. See [the authority guide](AUTHORITY_ARCHITECTURE.md) for build sources, credentials, session retries, namespace CAS and explicit migration. Existing snapshot publishers and agents continue to use the legacy profile until the distribution component is integrated.

### Managed authority delivery

The managed profile connects the agent to the authenticated authority endpoint and publishes durable immutable generations. Legacy `source_address` delivery remains available. Select the managed profile with `detent-agent --managed managed.json --generation-dir ./agent-state`; readers use the public generation directory and never open its credential configuration.

Provision `managed.json` as a regular, single-link file owned by root or the service account, with mode `0600` or controlled group-readable `0640`. Its required fields are:

```json
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

Use the identities and reader credential configured on your authority. The scope identity is derived from the principal, policy epoch (default `1`), and sorted unique namespaces. Optional `address`, `port`, and TLS `hostname` default to `127.0.0.1`, `8317`, and `localhost`; the address must be an IP literal. Relative CA paths resolve against the credential file. Certificate verification is required.

Initialize an empty directory explicitly once. Missing directory components are provisioned on the configuration worker with child/parent namespace barriers; existing permissions are preserved:

```sh
./build/src/detent-agent --managed managed.json --generation-dir ./agent-state --initialize-managed
```

Later starts omit `--initialize-managed` and require an existing managed store. `--upgrade-managed` explicitly adopts a compatible legacy generation directory. Neither mode silently recovers by discarding unreadable state. `--daemonize` backgrounds user operation; supervised system services run in the foreground.

A public `agent.json` can select `managed_path` and `generation_dir`. Set all four public identity fields (`generation_cluster`, `generation_group`, `generation_epoch`, `generation_scope`) to bind shell reads and status to the intended authority scope. Their flag equivalents are `--generation-cluster`, `--generation-group`, `--generation-epoch`, and `--generation-scope`; partial identities are rejected. Identity strings use lowercase hexadecimal (32 characters for cluster/group/epoch, 64 for scope). The SDK source accepts the corresponding expected cursor identity. Distribute this public identity separately from the private token.

`detent get NAME --generation-dir ./agent-state` reads the selected durable generation even while the agent is stopped. With a managed profile configured, user reads can auto-start a missing agent; `--system` leaves process startup to the service manager. Defaults remain optional. Missing live keys fall back to defaults, while invalid live types remain errors. `detent-agent --status --generation-dir ./agent-state` reports durable revision and count independently of the process status.

Optional bounds default to 256 MiB managed heap, 512 MiB mapped virtual space, 1 GiB named disk space, 4,096 disk entries, and 1,024 generations. Configure them with `heap_bytes`, `mapping_bytes`, `disk_bytes`, `disk_entries`, and `disk_generations`. These accounts exclude immutable configuration/startup parsing, Asio/TLS internals, thread stacks, and operating-system memory. `poll_interval_ms` defaults to 200 and `timeout_ms` to 60,000; `compression` defaults to true. Unknown or duplicate fields are rejected. `artifact_mode` accepts decimal `384`, `416`, or `420` (octal `0600`, `0640`, or `0644`); user mode defaults to `0600`, system mode to `0644`.

Changing authority identity requires an explicit `reset_from` object containing the old `cluster`, `group`, `epoch`, and `scope`. The selected old identity must match it. A trusted reset publishes a new snapshot while preserving the local storage UUID; an unconfigured transition fails startup. This is an administrative trust decision, not an automatic fallback.

Fresh macOS provisioning requires all traversed writable namespace edges to be on one filesystem, so the store's final owner-file `F_FULLFSYNC` covers them, including interrupted initialization retries. Read-only system ancestors are skipped only after filesystem inspection. An unsupported cross-filesystem path fails initialization; start from the intended volume and use a relative generation path. A preexisting final symlink is rejected, including one spelled with a trailing slash. Existing trusted ancestor links, such as macOS `/var`, remain supported.

The coordinator reports one `worker_pending` slot. Accepted recovery work owns its immutable directory and limits and survives close or loss of the caller's facade until the actual worker returns. Infrastructure failure that prevents original I/O or worker drainage follows an explicit fail-stop policy; it never reports successful settlement. Normal delivery failures preserve the durable selection and can retry.

## Managed authority endpoint pools

A managed source can set `authority_endpoints` to an array of up to eight objects such as `[{"address":"127.0.0.1","port":8317},{"address":"127.0.0.2","port":8317}]`, replacing the top-level `address` and `port`. Destinations must be distinct IP addresses with nonzero ports. All entries share the configured TLS hostname, CA, credential, authority identity and namespace scope.

The coordinator tries members serially and stays on a successful member. Transport failures, unavailable authorities and follower read rejection permit rotation. Admission pressure and local storage failures retain the route. Authentication, authorization, TLS verification and unexpected identity or profile fail closed, including denials received while closing a session. Sessions and publication feedback remain bound to their opening endpoint.

For pooled subscriptions, `quorum_refresh_ms` defaults to 30000 and accepts 1000 through 86400000. Refresh uses bounded jitter of ±20%; idle responses do not postpone it. A fresh Open revalidates authority quorum after accepted local publication and recovery settle. Polling already performs quorum reads. Discovery time includes refresh, per-member request deadlines, retry backoff and accepted local work; the default 60-second request timeout provides no short failover guarantee.

Pools contain authority endpoints. Relay Open does not establish current authority quorum. An agent consuming a relay must use a single relay endpoint and explicit `"delivery_mode":"subscription"`.

Both polling and subscription delivery stop on terminal trust denial while retaining the last-known-good durable generation.

## Managed reader capacity

Public `agent.json` can configure managed `detent get` and `detent-agent --status` independently of the daemon's private delivery credentials. Each setting also accepts a flag and environment variable: `reader_image_bytes`, for example, becomes `--reader-image-bytes` and `DETENT_READER_IMAGE_BYTES`. Flags override the environment, which overrides the file.

| Setting | Default | Bound |
| --- | --- | --- |
| `reader_records` | 500,000 | Positive 32-bit record count |
| `reader_namespaces` | 4,096 | Positive 32-bit namespace count |
| `reader_overrides` | 256 | 0–256 per definition |
| `reader_image_bytes` | 256 MiB | Positive signed 64-bit file extent |
| `reader_string_bytes` | 128 MiB | Positive signed 64-bit byte count |
| `reader_pages` | Existing paged format default | Positive 64-bit page count |
| `reader_segments` | Existing paged format default | Positive 64-bit segment count |
| `reader_heap_bytes` | 8 MiB | Positive host allocation width |
| `reader_mapping_bytes` | 512 MiB | Positive host allocation width |
| `reader_owners` | 16,384 | Positive host-width resident ownership count, subject to domain admission |

Without explicit reader settings, existing reader defaults and shared resources remain unchanged. Explicit settings create command-owned heap, retirement and mapping resources; clients, sources and views close before those resources drain. The retirement runner retains its separate 1 MiB baseline budget. Page, segment and image maxima remain independent; actual allocations and mappings still require ledger admission.

A file supplying any `reader_*` field must be a regular file owned by root or the effective user, without group/other write permission or a final symlink. The loader checks the descriptor it reads and rejects a changed file extent. World-readable mode `0644` is allowed; credentials belong in the separate private managed configuration. The bounded legacy configuration path keeps its existing symlink behavior when no reader settings are present.

The image never supplies its own limits. Keep generation identity binding configured, and fund the reader separately when increasing daemon capacity. These settings do not resize the legacy shared-memory image.
