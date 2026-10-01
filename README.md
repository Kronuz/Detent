# Detent (C++)

Detent is a C++ runtime-configuration client for namespaced boolean and
integer knobs, plus an HTTP distribution layer (`detentd`) and a host-local
shared-memory agent (`detent-agent`). It implements the model in
[`justknobs-implementation-spec.md`](justknobs-implementation-spec.md) and
adds a concrete publisher/distributor path, PATCH-based partial updates,
diff-since-last-version polling, cached compression, and optional Raft-based
replication for `detentd`.

**New here?** [`QUICKSTART.md`](QUICKSTART.md) walks through four current
deployment shapes: baked defaults only, one distributor with live updates,
host-local non-C++ reads through `detent-agent`, and a three-node Raft
cluster.

## What this is

- The steady-state read path is process-local and allocation-free: one atomic
  snapshot load, one lookup, and scalar evaluation. It does not touch the
  network, disk, or a mutex.
- One background updater thread consumes either a file source or an HTTP poll
  source, validates off the read path, merges over baked defaults, and
  atomically publishes a new immutable snapshot.
- Bool knobs support exact-match group overrides and stable SHA-256 percentage
  rollouts. Integer knobs do not support per-group overrides in v1.
- Missing knobs are errors. CLI overrides (`name=value`) shadow both baked
  defaults and live data.
- `detentd` serves full snapshots, accepts partial updates, persists the last
  accepted snapshot to disk, and optionally replicates writes through
  `cluster::Raft`.
- `detent-agent` keeps one warm `detent::Client` and publishes a pre-parsed
  binary snapshot into shared memory for fast host-local reads by shell
  scripts or other non-C++ callers.

## Using it in your own application

### 1. Pull it in (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(detent
  GIT_REPOSITORY <this-repo-url>
  GIT_TAG        main)
FetchContent_MakeAvailable(detent)

target_link_libraries(my_app PRIVATE detent::detent)
# Only if you want the HTTP poll source:
target_link_libraries(my_app PRIVATE detent_http::detent_http)
```

This repository has no public remote configured yet. Use `SOURCE_DIR` or
`add_subdirectory()` until it does.

### 2. Write your baked defaults (`just_knobs.json`)

Ship this alongside your binary. Baked defaults are the local fallback before
any live snapshot arrives, and they are the place where new knob names are
introduced.

```json
{
  "schema_version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 }
  }
}
```

### 3. Initialize a `Client` and read knobs

```cpp
#include "detent/client.hh"

detent::InitOptions opts;
opts.defaults_path = "/etc/myapp/just_knobs.json";
// Leave source_path/source_poll unset for baked-defaults-only reads.

detent::Client client(std::move(opts));

bool killswitch = client.eval_or_panic("checkout:killswitch_new_path");
std::int64_t max_results = client.get_or_panic("search:max_results");

auto r = client.eval("checkout:killswitch_new_path");
if (r) {
    use(r.value());
} else {
    log(r.error().what());
}

bool enabled = client.eval_or_panic(
    "checkout:killswitch_new_path",
    /*hash_val=*/user_id,
    /*switch_val=*/user_group);
```

Construct one `Client` per process, or use `detent::global::init()` and the
`detent::global::*` helpers.

### 4. Pick a distribution mechanism (optional)

One `Client`, three source shapes:

- **None:** baked defaults only.
- **Local file:** set `source_path`, and Detent polls the file for changes.
- **HTTP distributor:** set `source_poll` with
  `detent_http::make_http_poll_source()`.

File source:

```cpp
opts.source_path = "/var/lib/myapp/live_knobs.json";
opts.poll_interval = std::chrono::milliseconds{200};
```

HTTP source:

```cpp
#include "detent_http/http_source.hh"

opts.source_poll =
    detent_http::make_http_poll_source("distributor-host", 52617);
opts.poll_interval = std::chrono::milliseconds{1000};
```

Against a Raft cluster (see "A multi-node cluster" below), pass every peer
instead of one host:port -- the overload picks a random node at
construction (so a fleet of clients spreads across the cluster) and fails
over to the next one if its current node stops answering:

```cpp
opts.source_poll = detent_http::make_http_poll_source({
    {"node-a", 8081}, {"node-b", 8081}, {"node-c", 8081},
});
```

`source_path` and `source_poll` are mutually exclusive.

### 5. Run and publish to a distributor

```sh
detentd 52617 /var/lib/detent/snapshot.json &
curl -X PUT localhost:52617/v1/snapshot \
  -d '{"schema_version":1,"version":1,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}}'
```

Clients polling that distributor converge to version 1 within one
`poll_interval`.

### 6. CLI overrides and testing

```cpp
opts.cli_overrides = {
    "checkout:killswitch_new_path=true",
    "search:max_results=999",
};
```

For tests, bypass files and HTTP entirely:

```cpp
#include "detent/testing.hh"

detent::Snapshot snap;
snap.knobs["checkout:killswitch_new_path"] =
    detent::BoolDef{.default_value = true};
auto guard = detent::testing::TestKnobs(snap).set_thread_local();
```

## Architecture

Detent has three runtime roles:

1. A publisher that emits JSON snapshots or PATCH payloads.
2. One or more `detentd` instances that persist and serve the current
   snapshot.
3. Readers, either linked directly against `detent` or reading through
   `detent-agent`.

System sketch:

```text
publisher
   |  PUT/PATCH /v1/snapshot
   v
+-------------------+      cluster::Raft over cluster::Bus      +-------------------+
| detentd A         |<----------------------------------------->| detentd B         |
| persisted state   |                                           | persisted state   |
+-------------------+                                           +-------------------+
          ^                                                               ^
          | GET /v1/snapshot (ETag + optional diff)                       |
          +----------------------------+----------------------------------+
                                       |
                     +-----------------+-----------------+
                     |                                   |
                     v                                   v
             app process with                      detent-agent
             detent::Client                        shared-memory publisher
                                                         |
                                                         v
                                               shell / non-C++ local readers
```

`src/detentd/CLUSTERING.md` is the authoritative clustering design document.
It covers the current Raft write path, transport, restart safety, locking,
and the multi-region plan.

## Scope vs. the spec

This repository implements the spec's client model and adds a concrete
distribution layer:

- `detent`: core client, schema validation, rollout hashing, overrides,
  publication, health.
- `detent_http`: client-side HTTP poll source.
- `detentd`: HTTP distributor with persistence, PATCH support, diff serving,
  cached compression, and optional clustering.
- `detent-agent`: host-local shared-memory publisher for non-C++ readers.
- Python and Rust bindings that wrap the same C++ core.

The wire format remains `schema_version: 1`. Full snapshots, PATCH payloads,
and diff responses all validate through the same schema machinery.

### The distribution layer is polling, not long-polling

`GET /v1/snapshot` is immediate. Pollers send `If-None-Match`, and an
unchanged snapshot returns `304 Not Modified` with no body. This matches the
current HTTP framework's synchronous handler model and keeps read capacity
bounded by short requests rather than parked worker threads.

### Cross-replica propagation

Set `Options::Cluster` programmatically or pass `--cluster` to `detentd` to
join a Raft cluster over `cluster::Bus` multicast.

- Raft elects one leader.
- Any node can receive an HTTP PUT or PATCH; `cluster::Raft` forwards the log
  entry to the leader when needed.
- A write is reported accepted once the entry commits on a quorum.
- Every node applies the same committed log entry through
  `resolve_and_commit()`.
- `GET /v1/health` adds `clustered`, `is_leader`, and `leader_address` in
  clustered mode.
- Clustered PUT and PATCH responses include `X-Detent-Leader` as a hint about
  the current leader.

`src/detentd/CLUSTERING.md` contains the full design. The README keeps only
the surface behavior.

## Dependency graph

```text
detent
  |-- atomic-shared-ptr
  |-- cpp-btree
  |-- sha256 (+ endian)
  |-- char-classify
  |-- strict-stox
  `-- random

detent_http
  |-- detent
  `-- compressors   # Accept-Encoding: zstd, gzip on its own GET requests

detentd_lib
  |-- detent
  |-- detent_http
  |-- http (+ reactor, http-parser, radix-router, compressors)
  `-- cluster

detent_agent_lib
  `-- detent

detent-agent (exe)
  |-- detent_agent_lib
  `-- detent_http   # for --source-http
```

`detent` has no networking dependencies. `detent_http` is plain POSIX-socket
client code plus one codec library (no Asio/HTTP-framework weight) so it can
actually request the compression `detentd` already serves. `detentd` pulls
the HTTP framework plus `cluster` for Raft transport. `detent-agent`'s
library stays local-only; only the CLI executable links `detent_http` for
`--source-http`.

## Dependencies

All dependencies are fetched with CMake `FetchContent`.

| Library | Role |
|---|---|
| [`atomic-shared-ptr`](https://github.com/Kronuz/atomic-shared-ptr) | Snapshot publication |
| [`cpp-btree`](https://github.com/Kronuz/cpp-btree) | Immutable `KnobName -> KnobDef` map |
| [`sha256`](https://github.com/Kronuz/sha256) (+ `endian`) | Stable rollout-bucket hash |
| [`char-classify`](https://github.com/Kronuz/char-classify) | Knob-name validation |
| [`strict-stox`](https://github.com/Kronuz/strict-stox) | Strict integer parsing |
| [`random`](https://github.com/Kronuz/random) | Full-jitter reconnect backoff |
| [`compressors`](https://github.com/Kronuz/compressors) | zstd/gzip: `detent_http` client-side decode, `detentd` server-side encode |
| [RapidJSON](https://github.com/Tencent/rapidjson) | Strict JSON parsing |
| [`http`](https://github.com/Kronuz/http) (+ `reactor`, `http-parser`, `radix-router`) | `detentd`'s HTTP service |
| [`cluster`](https://github.com/Kronuz/cluster) | `detentd`'s Raft transport over multicast |
| standalone Asio | Pulled transitively by `http` and `cluster` |

RapidJSON runs in strict mode here. Unknown fields and malformed snapshots are
rejected rather than partially applied.

## Layout

```text
include/
  detent/
  detent_http/

src/
  detent/
  detent_http/
  detentd/
  detent_agent/

tests/
  detent/        8 suites
  detentd/       4 suites
  detent_agent/  2 suites

examples/
benchmarks/
diagrams/
bindings/
```

## What each `src/` subdirectory actually builds

There are six build artifacts but only two runnable daemons:

```text
detent
  ^
  +-- detent_http
  |
  +-- detentd_lib ---> detentd
  |
  `-- detent_agent_lib ---> detent-agent
```

- `detent` is the shared core.
- `detent_http` is the client-side HTTP poller.
- `detentd_lib` contains distributor logic so tests can link it directly.
- `detent_agent_lib` contains the shared-memory agent logic so tests can link
  it directly.
- `detentd` is the network-facing distributor.
- `detent-agent` is the host-local daemon and lifecycle CLI.

### Why four libraries and not one

- `detent` stays free of networking dependencies.
- `detent_http` is reused by both `detentd` and `detent-agent`.
- `detentd_lib` and `detent_agent_lib` let tests exercise the daemon logic
  in-process instead of through subprocesses.

## Binaries and their dependencies

| Binary | Built from | Links | What it is |
|---|---|---|---|
| `detent_demo` | `examples/demo.cc` | `detent` | Baked-defaults-only example |
| `detent_publish` | `examples/publish_snapshot.cc` | `detent` | Atomic file publisher |
| `detentd` | `src/detentd/main.cc` | `detentd_lib` -> `detent`, `detent_http`, `http::http`, `cluster::cluster` | HTTP distributor with optional Raft clustering |
| `detent-agent` | `src/detent_agent/main.cc` | `detent_agent_lib` -> `detent`; CLI also links `detent_http` | Host-local daemon and CLI |
| `test_*` (8, `tests/detent/`) | `tests/detent/*.cc` | `detent` | Core correctness suite |
| `test_distributor_*`, `test_load_fanout`, `test_raft_cluster` (4, `tests/detentd/`) | `tests/detentd/*.cc` | `detentd_lib` | Distributor correctness, end-to-end polling, load measurement, and real Raft cluster coverage |
| `test_agent_basic`, `test_shm_concurrency_stress` (2, `tests/detent_agent/`) | `tests/detent_agent/*.cc` | `detent_agent_lib` | Shared-memory agent correctness and stress |

## Build

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/examples/detent_demo
```

`DETENT_BUILD_DISTRIBUTOR` defaults to `ON`. Set
`-DDETENT_BUILD_DISTRIBUTOR=OFF` to skip `detentd` and its HTTP/cluster
dependency chain. `DETENT_BUILD_PYTHON` and `DETENT_BUILD_RUST` default to
`OFF`.

### Binary size

Measured on macOS/arm64 for `detent-agent`:

| Build | Size |
|---|---|
| Unset `CMAKE_BUILD_TYPE` | 1.15 MB |
| `Release` | 295 KB |
| `Release` + `strip -x` | 242 KB |

Most of the difference comes from RapidJSON and ordinary C++ runtime support.

### Running the distributor

Single node:

```sh
./build/src/detentd 52617 /var/lib/detent/snapshot.json
curl -X PUT localhost:52617/v1/snapshot \
  -d '{"schema_version":1,"version":1,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}}'
curl localhost:52617/v1/snapshot
curl localhost:52617/v1/health
```

Three same-host replicas, using the exact `--cluster` syntax from
`src/detentd/main.cc`:

```sh
./build/src/detentd 8081 /tmp/jk-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083
./build/src/detentd 8082 /tmp/jk-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083
./build/src/detentd 8083 /tmp/jk-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082
```

### Partial updates (PATCH) and diff-since-last-version polling

`PATCH /v1/snapshot` accepts the same top-level shape as a full snapshot, but
only the changed knobs need to be present. A `null` knob value deletes that
knob from the live snapshot.

```sh
curl -X PUT localhost:52617/v1/snapshot -d '{"schema_version":1,"version":1,"knobs":{
  "checkout:killswitch_new_path":{"type":"bool","default":false},
  "search:max_results":{"type":"int","default":25},
  "payments:new_processor":{"type":"bool","default":false,"rollout_pct":10}
}}'

curl -X PATCH localhost:52617/v1/snapshot -d '{"schema_version":1,"version":2,"knobs":{
  "checkout:killswitch_new_path":{"type":"bool","default":true}
}}'
```

Pollers can ask for a diff from any version they last accepted by sending
`X-Detent-Base-Version`. A request one update behind hits a precomputed
cache (no per-request work). A request further behind gets a diff composed
from up to 64 retained per-step patches, still smaller than the full body
in the common case; beyond that window, or with no header at all, the
response is the full snapshot. Either way the response carries
`Content-Type: application/merge-patch+json` and
`X-Patch-Base-Version: <base>` when it's a diff.

Measured example:

- Full body: 276 bytes
- Cached diff for the same update: 138 bytes
- Gzip-compressed full body: 173 bytes

Compression is cached per accepted update and negotiated with
`Accept-Encoding`. Bodies below the HTTP library's 256-byte compression
threshold stay uncompressed.

`detent_http`'s own client (`make_http_poll_source`, what `detent::Client`
uses for `--source-http`) sends `Accept-Encoding: zstd, gzip` on every GET
and decodes the response transparently -- the caller never sees compressed
bytes. `detentd` prefers zstd over gzip on a tie, so a `detent_http` poller
gets zstd whenever the server has it available (it always does).

### Running detentd

```sh
./build/src/detentd
```

The distributor defaults to port `52617` and persists accepted snapshots in
`$XDG_STATE_HOME/detent/snapshot.json` (otherwise
`~/.local/state/detent/snapshot.json` on Linux/FreeBSD or
`~/Library/Application Support/Detent/state/snapshot.json` on macOS).
It creates the directory automatically. Before the first publish, the
snapshot endpoint returns `404`.

Optional `detentd.json` lives in the same platform config directory as
`agent.json`. It accepts `port`, `persist_path`, `reactors`, and `workers`.
Relative paths are resolved against the config file. `--config PATH` selects
another file. CLI arguments override `DETENTD_*` environment variables,
which override config. Both `detentd --port 52617 --persist-path PATH` and
legacy `detentd 52617 PATH` work. See [QUICKSTART](QUICKSTART.md#scenario-2-one-distributor-live-updates)
for a complete publishing example.

### Running detent-agent

With a local distributor on `127.0.0.1:52617` and a published snapshot:

```sh
./build/src/detent get search:max_results
./build/src/detent-agent status
./build/src/detent-agent stop
```

`get` starts the agent automatically. Defaults and environment variables are
optional. For persistent settings, put `agent.json` in the platform's user
config directory (`~/.config/detent` on Linux/FreeBSD,
`~/Library/Application Support/Detent` on macOS):

```json
{"source_address": "127.0.0.1:52617", "poll_interval_ms": 200}
```

`--config PATH` selects another config file. `--source-address HOST:PORT` and
`DETENT_SOURCE_ADDRESS` override the configured address. The transport is HTTP;
`--source-http` and `DETENT_SOURCE_HTTP` remain aliases.

Optional `defaults.json` beside the config supplies local fallback knobs.
`--defaults PATH` selects another file. Without defaults, the first read waits
up to two seconds for a source snapshot; `--default VALUE` provides a per-read
fallback. With only defaults configured, the agent serves local-only reads.

Runtime files use `$XDG_RUNTIME_DIR/detent`, falling back to a private
`detent-<uid>` directory under the system temporary directory. `runtime_dir`,
`--runtime-dir`, or `DETENT_RUNTIME_DIR` selects another directory. Stop the
agent to apply changed startup settings. See [QUICKSTART](QUICKSTART.md#persistent-agent-configuration)
for system config locations and precedence.

### IPC design: shared memory, not a socket

`detent-agent` publishes a flat binary snapshot into a memory-mapped file
under a seqlock. Readers map that file read-only and evaluate directly
against the published bytes.

Measured throughput on the same development machine:

| Benchmark | Result |
|---|---|
| `detent_read_bench 4 2000000` | 6.71M ops/sec (149.1 ns/op) |
| `detent_agent_shm_bench 4 2000000` | 36.2M ops/sec (27.6 ns/op) |

The shared-memory read path is about 5.4x faster in that benchmark because it
avoids JSON parsing, avoids a daemon round trip, and reads a pre-parsed binary
layout.

Two implementation constraints remain relevant:

1. The reader bounds-checks all offsets and lengths against `payload_len`
   before dereferencing them.
2. The seqlock publication protocol is correct for the targeted platforms, but
   the writer's plain `memcpy` and the readers' plain loads are still outside a
   formal race-free C++ object-model proof.

## Language bindings

Enable bindings explicitly:

```sh
cmake -B build -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON
cmake --build build -j
```

- **Python** (`bindings/python/`, pybind11): wraps `detent::Client` and raises
  `detent.DetentError` on construction or read failures.
- **Rust** (`bindings/rust/`, `cxx`): wraps `detent::Client` behind an opaque
  handle and returns `Result<T, DetentError>`.

Python example:

```python
import detent

client = detent.Client(
    "/etc/myapp/just_knobs.json",
    source_path="/var/lib/myapp/live.json",
)
client.eval("payments:new_processor", hash_val="user-42")
client.eval("checkout:new_flow", switch_val="beta_testers")
client.get("search:max_results")
client.health()
```

Rust example:

```rust
let client = detent::Client::new(
    "/etc/myapp/just_knobs.json",
    Some("/var/lib/myapp/live.json"),
    vec![],
    200,
)?;
client.eval("payments:new_processor", Some("user-42"), None)?;
client.eval("checkout:new_flow", None, Some("beta_testers"))?;
client.get("search:max_results", None)?;
client.health();
```

## Testing

The repository currently has 15 C++ test binaries:

- **Core library (`tests/detent/`, 8 suites):** schema/model validation,
  rollout hash golden vectors, eval semantics, CLI overrides, file-source
  behavior, thread-local test guard behavior, concurrency stress, and
  `SnapshotPatch` parse/apply/diff/serialize coverage.
- **Distributor (`tests/detentd/`, 5 suites):**
  - `test_distributor_basic`: PUT/GET/304/rejection semantics and persistence.
  - `test_distributor_client_integration`: real `detentd` ->
    `detent_http::make_http_poll_source` -> `detent::Client`, including cached
    diff delivery proven by `diffs_served()`.
  - `test_multi_node_poll_source`: the multi-node `make_http_poll_source`
    overload -- starts against one of two real distributors and fails over
    to the other once the first stops answering.
  - `test_load_fanout`: one distributor with many polling watchers.
  - `test_raft_cluster`: a real 3-node cluster covering leader election,
    follower-forwarded writes, PATCH replication, leader failover,
    restart-safety/rejoin, dead-follower liveness, and correlation-id
    uniqueness across a node's own restart.
- **Host-local agent (`tests/detent_agent/`, 2 suites):** shared-memory
  reader/writer correctness and a dedicated concurrency stress test.

These 15 all run in-process (one binary constructs every `Distributor`
directly) with a single fast, hardcoded Raft timing, and only ever stop a
node via a clean destructor call. `tests/e2e/` complements them with a
separate harness that spawns real `detentd` subprocesses, drives them over
real HTTP, and kills them with real signals -- see `tests/e2e/README.md`
for what it checks and why it exists; run it after any change to
`src/detentd/` or the vendored Raft wiring.

## Benchmarks

Benchmarks in `benchmarks/` build by default. Measured output on one 14-core
arm64 development machine:

| Benchmark | What it measures | Command | Result |
|---|---|---|---|
| `detent_read_bench` | Steady-state read throughput | `detent_read_bench 4 2000000` | 6.71M ops/sec (149.1 ns/op) |
| `detent_hash_bench` | `stable_bucket()` only | `detent_hash_bench 5000000` | 5.05M calls/sec (198.2 ns/call) |
| `detent_update_bench` | Parse + validate + merge for a 100-knob snapshot | `detent_update_bench 200000 100` | 28.6K updates/sec (35.0 us/update) |
| `detent_agent_shm_bench` | Shared-memory read throughput | `detent_agent_shm_bench 4 2000000` | 36.2M ops/sec (27.6 ns/op) |
| `detentd_fanout_bench` | Distributor fan-out capacity | `detentd_fanout_bench 1000 3000 200 4` | 1,000 watchers, ~17,000 requests served, 1000/1000 converged, p50 30-100/p90 190-210/p99 200-215/max 205-216 ms (run-to-run variance, see below) |

### Load/fan-out: what was actually measured

`tests/detentd/test_load_fanout` runs one distributor plus N simulated polling
watchers and measures the time between publishing an update and all watchers
observing it. `detentd_fanout_bench` runs the same harness standalone with a
configurable watcher count and duration; each row below is
`detentd_fanout_bench <watchers> 3000 200 4` (a 200 ms poll interval),
several runs each, on a shared, not-idle development machine.

| Watchers | p50 fan-out latency | p99 fan-out latency |
|---|---|---|
| 100 | ~156-165 ms | ~166-170 ms |
| 1,000 | ~30-100 ms | ~200-215 ms |
| 2,000 | ~105-145 ms | ~200 ms to 2.4 s |

The 100-watcher p50/p99 sit close together (most watchers converge in the
same poll cycle); 1,000 tracks the 200 ms poll interval loosely, with p50
swinging with scheduling noise; 2,000 still degrades unpredictably on this
single-machine harness, sometimes by 10x, and does not isolate whether the
bottleneck is the distributor or the load generator.

### Security boundary

`detentd` has no authentication, authorization, or TLS. Anyone that can reach
the HTTP port can publish a snapshot. Use an internal network boundary, a
reverse proxy, or add auth before using it outside a trusted environment.

The HTTP layer rejects oversized PUT/PATCH bodies before buffering more than
16 MiB, matching the JSON parser's byte-size cap.

### Consistency model

Within one Raft cluster, writes commit in a single replicated log and stale
versions are rejected cluster-wide. The current code does not implement the
multi-region plan described in `src/detentd/CLUSTERING.md`; the current
surface is one Raft cluster per distributor deployment.

## Production readiness: honest checklist

| Area | Status | Detail |
|---|---|---|
| Correctness (client) | **Proven** | 8 automated suites, including golden hash vectors, precedence rules, and concurrency stress |
| Correctness (distributor) | **Proven** | PUT/GET/PATCH semantics, persistence, cached diffs, and rejection cases are covered |
| Correctness (clustered distributor) | **Proven at small scale** | Real 3-node Raft test covers election, forwarding, failover, restart/rejoin, fresh-bootstrap latency, and dead-follower liveness |
| Correctness (host-local agent) | **Proven** | Shared-memory round trip and dedicated stress coverage |
| End-to-end polling path | **Proven** | Real `detentd` -> `detent_http` -> `detent::Client` test |
| Load/capacity | **Measured, single machine** | Clean through 1,000 pollers in the current harness; 2,000 is not isolated |
| Linux sanitizer coverage | **Proven on Linux** | ASan/UBSan and TSan run clean across the full suite (multiple stress repetitions); `src/detentd/CLUSTERING.md` documents the shutdown-order fix TSan found in `~Distributor()` |
| Security | **Boundary is explicit** | No auth or TLS on `detentd` |
| Real network conditions | **Not done** | Current measurements are loopback-only |
| Multi-region replication | **Not implemented** | Design exists in `CLUSTERING.md`; code path does not |
| Soak/long-run stability | **Not done** | No multi-hour or multi-day validation documented |


System-wide deployment: see [System services](docs/SERVICES.md) for service accounts, directory ownership, supervision, and logging on Linux, FreeBSD, and macOS.

The shell reader is `detent get NAME`. Agent lifecycle commands are `detent-agent start`, `stop`, `restart`, `status`, and `help`. `restart` runs a user agent in the background; managed services use their service manager. The reader launches a sibling `detent-agent` binary, or finds it on `PATH` when invoked by name.
