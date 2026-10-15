# Detent (C++)

Detent is a C++ runtime-configuration client for namespaced boolean and integer knobs, plus an HTTP distribution layer (`detentd`) and a host-local shared-memory agent (`detent-agent`). It implements the model in [`justknobs-implementation-spec.md`](justknobs-implementation-spec.md) and adds a concrete publisher/distributor path, PATCH-based partial updates, diff-since-last-version polling, cached compression, and optional Raft-based replication for `detentd`.

**New here?** [`QUICKSTART.md`](QUICKSTART.md) covers numeric settings, feature flags, gradual rollouts, group overrides, and per-read fallbacks. The [usage guide](docs/USAGE.md) covers network distribution, configuration, and library examples.

## What this is

- The steady-state read path is process-local and allocation-free: one atomic snapshot load, one lookup, and scalar evaluation. It does not touch the network, disk, or a mutex.
- One background updater thread consumes either a file source or an HTTP poll source, validates off the read path, merges over baked defaults, and atomically publishes a new immutable snapshot.
- Bool knobs support exact-match group overrides and stable SHA-256 percentage rollouts. Integer knobs do not support per-group overrides in v1.
- Missing knobs are errors. CLI overrides (`name=value`) shadow both baked defaults and live data.
- `detentd` serves full snapshots, accepts partial updates, persists the last accepted snapshot to disk, and optionally replicates writes through `cluster::Raft`.
- `detent-agent` keeps one warm `detent::Client` and publishes a pre-parsed binary snapshot into shared memory for fast host-local reads by shell scripts or other non-C++ callers.

## Using it in your own application

### 1. Pull it in (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(detent
  GIT_REPOSITORY https://github.com/Kronuz/Detent.git
  GIT_TAG        main)
FetchContent_MakeAvailable(detent)

target_link_libraries(my_app PRIVATE detent::detent)
# Only if you want the HTTP poll source:
target_link_libraries(my_app PRIVATE detent_http::detent_http)
```

For reproducible builds, replace `main` with the commit you have qualified. A local checkout can also be included with `add_subdirectory()`.

### 2. Write your baked defaults (`just_knobs.json`)

Ship this alongside your binary. Baked defaults are the local fallback before any live snapshot arrives, and they are the place where new knob names are introduced.

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

Construct one `Client` per process, or use `detent::global::init()` and the `detent::global::*` helpers.

### 4. Pick a distribution mechanism (optional)

One `Client`, three source shapes:

- **None:** baked defaults only.
- **Local file:** set `source_path`, and Detent polls the file for changes.
- **HTTP distributor:** set `source_poll` with `detent_http::make_http_poll_source()`.

File source:

```cpp
opts.source_path = "/var/lib/myapp/live_knobs.json";
opts.poll_interval = std::chrono::milliseconds{200};
```

HTTP source:

```cpp
#include "detent_http/http_source.hh"

opts.source_poll =
    detent_http::make_http_poll_source("distributor-host", 8317);
opts.poll_interval = std::chrono::milliseconds{1000};
```

Against a Raft cluster (see "A multi-node cluster" below), pass every peer instead of one host:port -- the overload picks a random node at construction (so a fleet of clients spreads across the cluster) and fails over to the next one if its current node stops answering:

```cpp
opts.source_poll = detent_http::make_http_poll_source({
    {"node-a", 8081}, {"node-b", 8081}, {"node-c", 8081},
});
```

`source_path` and `source_poll` are mutually exclusive.

### 5. Run and publish to a distributor

```sh
detentd 8317 /var/lib/detent/snapshot.json &
curl -X PUT localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":1,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}}'
```

Clients polling that distributor converge to version 1 within one `poll_interval`.

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
2. One or more `detentd` instances that persist and serve the current snapshot.
3. Readers, either linked directly against `detent` or reading through `detent-agent`.

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

`src/detentd/CLUSTERING.md` is the authoritative clustering design document. It covers the current Raft write path, transport, restart safety, locking, and the multi-region plan.

## Scope vs. the spec

This repository implements the spec's client model and adds a concrete distribution layer:

- `detent`: core client, schema validation, rollout hashing, overrides, publication, health.
- `detent_http`: client-side HTTP poll source.
- `detentd`: HTTP distributor with persistence, PATCH support, diff serving, cached compression, and optional clustering.
- `detent-agent`: host-local shared-memory publisher for non-C++ readers.
- Python and Rust bindings that wrap the same C++ core.

The wire format remains `schema_version: 1`. Full snapshots, PATCH payloads, and diff responses all validate through the same schema machinery.

### The distribution layer is polling, not long-polling

`GET /v1/snapshot` is immediate. Pollers send `If-None-Match`, and an unchanged snapshot returns `304 Not Modified` with no body. This matches the current HTTP framework's synchronous handler model and keeps read capacity bounded by short requests rather than parked worker threads.

### Cross-replica propagation

Set `Options::Cluster` programmatically or pass `--cluster` to `detentd` to join a Raft cluster over `cluster::Bus` multicast.

- Raft elects one leader.
- Any node can receive an HTTP PUT or PATCH; `cluster::Raft` forwards the log entry to the leader when needed.
- A write is reported accepted once the entry commits on a quorum.
- Every node applies the same committed log entry through `resolve_and_commit()`.
- `GET /v1/health` adds `clustered`, `is_leader`, and `leader_address` in clustered mode.
- Clustered PUT and PATCH responses include `X-Detent-Leader` as a hint about the current leader.

`src/detentd/CLUSTERING.md` contains the full design. The README keeps only the surface behavior.

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

`detent` has no networking dependencies. `detent_http` is plain POSIX-socket client code plus one codec library (no Asio/HTTP-framework weight) so it can actually request the compression `detentd` already serves. `detentd` pulls the HTTP framework plus `cluster` for Raft transport. `detent-agent`'s library stays local-only; only the CLI executable links `detent_http` for `--source-http`.

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

RapidJSON runs in strict mode here. Unknown fields and malformed snapshots are rejected rather than partially applied. It is pinned to upstream commit `24b5e7a8b27f42fa16b96fc70aade9106cf7102f`; the older 1.1.0 release contains a `GenericStringRef` assignment operator that fails to compile with Clang 23.

## Layout

```text
include/
  detent/              Core client and evaluation API
  detent_http/         Classic HTTP polling source
  detent_distribution/ Binary protocol, storage and shared sessions
  detent_distribution_http/ Optional server grammar and owned replies
  detent_relay/        Durable cache and session adapters
  detent_local/        Immutable views and durable generations
  detent_delivery/     TLS client and publication coordinator
  detent_transport/    TLS configuration

src/
  detent/              Core implementation
  detent_http/         Classic HTTP polling
  detentd/             Distributor host and classic Raft adapter
  detent_agent/        Agent host, shared memory and managed configuration
  detent_authority/    Durable authority, transactions and distribution
  detent_distribution/ Protocol and session implementation
  detent_distribution_http/ Shared server parsing and reply pumping
  detent_relay/        Durable relay core
  detent_local/        Image building and persistence
  detent_delivery/     Session transport and coordinator
  detent_cli/          Shared configuration, logging and daemon lifecycle

tests/                 Matching component suites and e2e process harness
examples/
benchmarks/
diagrams/
bindings/
```

## What the source modules build

The core client has no networking dependency. Classic HTTP polling, binary distribution, immutable local views, durable publication and managed TLS delivery are separate targets, so consumers can select the source and storage profile they need. Authority implementation targets remain internal to the daemon and its tests.

Managed agents and relay bundle sources accept `"image_format": "paged-v2"` for incremental immutable local publication; `flat-v1` remains the default. Both formats remain readable by the current SDK, and existing flat heads migrate before paged updates. See [writer preference and maintenance](docs/LOCAL_IMAGE_FORMAT.md#writer-preference-and-maintenance) for compaction controls, disk coexistence and reader compatibility.

| Module | Main target | Purpose |
| --- | --- | --- |
| Core client | `detent::detent` | Local lookup, evaluation, defaults and file updates |
| Classic HTTP source | `detent_http::detent_http` | Optional JSON polling source |
| Binary protocol | `detent::distribution` | Canonical codecs, bounded storage, staged frames and shared session lifecycle |
| Distribution server transport | `detent::distribution_http` | Optional strict request parsing and owned HTTP replies |
| Relay HTTP runtime | `detent::relay_http` | Bounded authenticated intake and shared-frame replies |
| Relay host | `detent_relay_host`, `detent-relay` | Optional finite-bundle TLS daemon; [qualification and configuration](docs/RELAY.md) |
| Relay core | `detent::relay` | Durable bundle caches and authorized session adapters |
| Immutable local view | `detent::local_view` | Read mapped durable generations |
| Pinned local export | `detent::local_export` | Encode public definitions from a recovered immutable image |
| Local publication | `detent::local_publication` | Reconcile and publish durable generations |
| Managed transport | `detent::delivery_transport` | Incremental TLS delivery and sessions |
| Managed coordinator | `detent::delivery_coordinator` | Join source delivery to local publication |
| Private source configuration | `detent::delivery_config` | Validate source credentials, scope, TLS and publication limits |
| Distributor and agent hosts | `detentd_lib`, `detent_agent_lib` | Reuse daemon logic in embedded tests |

## Binaries and their dependencies

| Binary | Built from | Links | What it is |
|---|---|---|---|
| `detent_demo` | `examples/demo.cc` | `detent` | Baked-defaults-only example |
| `detent_publish` | `examples/publish_snapshot.cc` | `detent` | Atomic file publisher |
| `detentd` | `src/detentd/main.cc` | `detentd_lib` -> `detent`, `detent_http`, `http::http`, `cluster::cluster` | HTTP distributor with optional Raft clustering |
| `detent-relay` | `src/detent_relay/main.cc` | Relay host, private configuration and bounded TLS distribution | Optional durable fan-out daemon; Stage 5B qualification |
| `detent-agent` | `src/detent_agent/main.cc` | Agent, CLI and optional managed delivery libraries | Host-local daemon |
| `detent` | `src/detent_agent/main.cc` | CLI and local reader libraries | Host-local `get` reader |
| `test_*` (`tests/detent/`) | `tests/detent/*.cc` | `detent` | Core correctness suite |
| Distributor tests (`tests/detentd/`) | `tests/detentd/*.cc` | `detentd_lib` | Distributor correctness, end-to-end polling, load measurement, and real Raft cluster coverage |
| Agent tests (`tests/detent_agent/`) | `tests/detent_agent/*.cc` | `detent_agent_lib` | Shared-memory agent correctness and stress |

## Build

The C++20 standard library must provide `std::jthread` and stop tokens; configuration checks this before building. libc++ 20 and newer expose these normally. FreeBSD 14.4 with base libc++ 19 requires an explicit `-DCMAKE_CXX_FLAGS=-fexperimental-library` when configuring; this opts into libc++'s experimental API/ABI and must also be used by consumers of the public C++ client headers. Prefer a stable implementation when available. See [LLVM 19 documentation](https://releases.llvm.org/19.1.0/projects/libcxx/docs/UsingLibcxx.html) and [LLVM 20 release notes](https://libcxx.llvm.org/ReleaseNotes/20.html).

The authority integration tests require Python 3, Bash, curl, and the OpenSSL command-line tool on `PATH`. Bash is not part of FreeBSD's base system; install it before running the tests. These tools are test dependencies and are not required by the running daemons.

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/examples/detent_demo
```

`DETENT_BUILD_DISTRIBUTOR` defaults to `ON`. Set `-DDETENT_BUILD_DISTRIBUTOR=OFF` to skip `detentd` and its HTTP/cluster dependency chain. `DETENT_BUILD_PYTHON` and `DETENT_BUILD_RUST` default to `OFF`.

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
./build/src/detentd 8317 /var/lib/detent/snapshot.json
curl -X PUT localhost:8317/v1/snapshot \
  -d '{"schema_version":1,"version":1,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}}'
curl localhost:8317/v1/snapshot
curl localhost:8317/v1/health
```

Three same-host replicas, using the exact `--cluster` syntax from `src/detentd/main.cc`:

```sh
mkdir -p .scratch
./build/src/detentd 8081 .scratch/cluster-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083
./build/src/detentd 8082 .scratch/cluster-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083
./build/src/detentd 8083 .scratch/cluster-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082
```

### Partial updates (PATCH) and diff-since-last-version polling

`PATCH /v1/snapshot` accepts the same top-level shape as a full snapshot, but only the changed knobs need to be present. A `null` knob value deletes that knob from the live snapshot.

```sh
curl -X PUT localhost:8317/v1/snapshot -d '{"schema_version":1,"version":1,"knobs":{
  "checkout:killswitch_new_path":{"type":"bool","default":false},
  "search:max_results":{"type":"int","default":25},
  "payments:new_processor":{"type":"bool","default":false,"rollout_pct":10}
}}'

curl -X PATCH localhost:8317/v1/snapshot -d '{"schema_version":1,"version":2,"knobs":{
  "checkout:killswitch_new_path":{"type":"bool","default":true}
}}'
```

Pollers can ask for a diff from any version they last accepted by sending `X-Detent-Base-Version`. A request one update behind hits a precomputed cache (no per-request work). A request further behind gets a diff composed from up to 64 retained per-step patches, still smaller than the full body in the common case; beyond that window, or with no header at all, the response is the full snapshot. Either way the response carries `Content-Type: application/merge-patch+json` and `X-Patch-Base-Version: <base>` when it's a diff.

Measured example:

- Full body: 276 bytes
- Cached diff for the same update: 138 bytes
- Gzip-compressed full body: 173 bytes

Compression is cached per accepted update and negotiated with `Accept-Encoding`. Bodies below the HTTP library's 256-byte compression threshold stay uncompressed.

`detent_http`'s own client (`make_http_poll_source`, what `detent::Client` uses for `--source-http`) sends `Accept-Encoding: zstd, gzip` on every GET and decodes the response transparently -- the caller never sees compressed bytes. `detentd` prefers zstd over gzip on a tie, so a `detent_http` poller gets zstd whenever the server has it available (it always does).

### Running detentd

```sh
./build/src/detentd
```

The distributor defaults to port `8317` and persists accepted snapshots in `$XDG_STATE_HOME/detent/snapshot.json` (otherwise `~/.local/state/detent/snapshot.json` on Linux/FreeBSD or `~/Library/Application Support/Detent/state/snapshot.json` on macOS). It creates the directory automatically. Before the first publish, the snapshot endpoint returns `404`.

Optional `detentd.json` lives in the same platform config directory as `agent.json`. It accepts `port`, `persist_path`, `reactors`, and `workers`. Relative paths are resolved against the config file. `--config PATH` selects another file. CLI arguments override `DETENTD_*` environment variables, which override config. Both `detentd --port 8317 --persist-path PATH` and legacy `detentd 8317 PATH` work. See [the usage guide](docs/USAGE.md#scenario-2-one-distributor-live-updates) for HTTP publishing and configuration.

### Running detent-agent

With a local distributor on `127.0.0.1:8317` and a published snapshot:

```sh
./build/src/detent get search:max_results
./build/src/detent-agent --status
kill -TERM "$(./build/src/detent-agent --status | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

`get` starts the agent automatically. Defaults and environment variables are optional. For persistent settings, put `agent.json` in the platform's user config directory (`~/.config/detent` on Linux/FreeBSD, `~/Library/Application Support/Detent` on macOS):

```json
{"source_address": "127.0.0.1:8317", "poll_interval_ms": 200}
```

`--config PATH` selects another config file. `--source-address HOST:PORT` and `DETENT_SOURCE_ADDRESS` override the configured address. The transport is HTTP; `--source-http` and `DETENT_SOURCE_HTTP` remain aliases.

Optional `defaults.json` beside the config supplies local fallback knobs. `--defaults PATH` selects another file. Without defaults, the first read waits up to two seconds for a source snapshot; `--default VALUE` provides a per-read fallback. With only defaults configured, the agent serves local-only reads.

Runtime files use `$XDG_RUNTIME_DIR/detent`, falling back to a private `detent-<uid>` directory under the system temporary directory. `runtime_dir`, `--runtime-dir`, or `DETENT_RUNTIME_DIR` selects another directory. Stop the agent to apply changed startup settings. See [the usage guide](docs/USAGE.md#persistent-agent-configuration) for system config locations and precedence.

### IPC design: shared memory, not a socket

`detent-agent` publishes a flat binary snapshot into a memory-mapped file under a seqlock. Readers map that file read-only and evaluate directly against the published bytes.

Measured throughput on the same development machine:

| Benchmark | Result |
|---|---|
| `detent_read_bench 4 2000000` | 6.71M ops/sec (149.1 ns/op) |
| `detent_agent_shm_bench 4 2000000` | 36.2M ops/sec (27.6 ns/op) |

The shared-memory benchmark reports about 5.4 times the throughput in this run. Both benchmarks read local, already parsed data; neither parses JSON or contacts a daemon for each lookup. These results do not establish which implementation difference causes the ratio.

Two implementation constraints remain relevant:

1. The reader bounds-checks all offsets and lengths against `payload_len` before dereferencing them.
2. The seqlock publication protocol is correct for the targeted platforms, but the writer's plain `memcpy` and the readers' plain loads are still outside a formal race-free C++ object-model proof.

## Language bindings

Enable bindings explicitly:

```sh
cmake -B build -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON
cmake --build build -j
```

- **Python** (`bindings/python/`, pybind11): wraps `detent::Client` and raises `detent.DetentError` on construction or read failures.
- **Rust** (`bindings/rust/`, `cxx`): wraps `detent::Client` behind an opaque handle and returns `Result<T, DetentError>`.

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

CTest registers component and process tests for the enabled build profiles. Use `ctest --test-dir build -N` to list the exact set for your configuration.

1. Core tests cover schema validation, rollout vectors, evaluation, overrides, file updates and concurrency.
2. Distributor and HTTP tests cover persistence, polling, partial updates, failover, fan-out and classic Raft replication.
3. Authority tests cover transactions, durable storage, authenticated consensus, restart and failure recovery, bounded distribution, subscription admission and large-state idling.
4. Protocol, local-view and delivery tests cover binary validation, mapped generations, original IO settlement, publication reconciliation and exact source feedback.
5. Process fixtures start real daemons, exercise HTTP and local readers, and stop or kill tracked processes. `tests/e2e/README.md` describes the separate classic distributor harness.

## Benchmarks

Benchmarks in `benchmarks/` build by default. Measured output on one 14-core arm64 development machine:

| Benchmark | What it measures | Command | Result |
|---|---|---|---|
| `detent_read_bench` | Steady-state read throughput | `detent_read_bench 4 2000000` | 6.71M ops/sec (149.1 ns/op) |
| `detent_hash_bench` | `stable_bucket()` only | `detent_hash_bench 5000000` | 5.05M calls/sec (198.2 ns/call) |
| `detent_update_bench` | Parse + validate + merge for a 100-knob snapshot | `detent_update_bench 200000 100` | 28.6K updates/sec (35.0 us/update) |
| `detent_agent_shm_bench` | Shared-memory read throughput | `detent_agent_shm_bench 4 2000000` | 36.2M ops/sec (27.6 ns/op) |
| `detentd_fanout_bench` | Distributor fan-out capacity | `detentd_fanout_bench 1000 3000 200 4` | 1,000 watchers, ~17,000 requests served, 1000/1000 converged, p50 30-100/p90 190-210/p99 200-215/max 205-216 ms (run-to-run variance, see below) |

### Load/fan-out: what was actually measured

`tests/detentd/test_load_fanout` runs one distributor plus N simulated polling watchers and measures the time between publishing an update and all watchers observing it. `detentd_fanout_bench` runs the same harness standalone with a configurable watcher count and duration; each row below is `detentd_fanout_bench <watchers> 3000 200 4` (a 200 ms poll interval), several runs each, on a shared, not-idle development machine.

| Watchers | p50 fan-out latency | p99 fan-out latency |
|---|---|---|
| 100 | ~156-165 ms | ~166-170 ms |
| 1,000 | ~30-100 ms | ~200-215 ms |
| 2,000 | ~105-145 ms | ~200 ms to 2.4 s |

The 100-watcher p50/p99 sit close together (most watchers converge in the same poll cycle); 1,000 tracks the 200 ms poll interval loosely, with p50 swinging with scheduling noise; 2,000 still degrades unpredictably on this single-machine harness, sometimes by 10x, and does not isolate whether the bottleneck is the distributor or the load generator.

### Security boundary

`detentd` has no authentication, authorization, or TLS. Anyone that can reach the HTTP port can publish a snapshot. Use an internal network boundary, a reverse proxy, or add auth before using it outside a trusted environment.

The HTTP layer rejects oversized PUT/PATCH bodies before buffering more than 16 MiB, matching the JSON parser's byte-size cap.

### Consistency model

Within one Raft cluster, writes commit in a single replicated log and stale versions are rejected cluster-wide. The current code does not implement the multi-region plan described in `src/detentd/CLUSTERING.md`; the current surface is one Raft cluster per distributor deployment.

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

The shell reader is `detent get NAME`. Both daemons run directly in the foreground, accept `--daemonize` for user background operation, and provide read-only `--status`. Send SIGTERM to stop an unmanaged daemon; supervised services use their service manager. The reader launches a sibling `detent-agent` binary, or finds it on `PATH` when invoked by name.

### Rollouts by group

Boolean overrides may be `true` or `false`, or an object containing only `rollout_pct` (an integer from 0 to 100):

```json
"overrides": {"employees": true, "beta": {"rollout_pct": 50}, "production": {"rollout_pct": 5}}
```

A matching group rollout replaces the knob-wide rollout percentage and uses the same hash bucket. Without a hash, it returns the knob's default. Unknown groups use the knob-wide rollout. Boolean overrides still force their value. The behavior is shared by C++ clients, language bindings, and `detent get`. PATCH replaces a complete knob definition, including all its groups.

The shared-memory layout is now version 2. Stop and restart existing agents when upgrading; older readers cannot interpret group rollout records.

Daemon diagnostics: see [Logging](docs/LOGGING.md) for verbosity, terminal colors, service configuration, and measured footprint. The logger is linked only into daemon executables.

Large-state architecture and implementation gates: see the [Validated Architectural Blueprint](docs/VALIDATED_ARCHITECTURE.md) and [Scaling Detent](docs/SCALING.md), including the measured 100,000/500,000-knob baseline and the Xapiand compatibility plan. The blueprint is a source-reviewed, conditional architecture approval; consensus correctness, durability, and fleet capacity still require the specified validation gates.

The [production operating contract](docs/PRODUCTION_CONTRACT.md) states topology, retry retention, staleness, restore and upgrade boundaries. Current work and reproduced regression evidence are tracked in the [implementation record](docs/IMPLEMENTATION.md).

The HTTP polling source includes `<exception>` directly for its captured `std::exception_ptr` and `std::current_exception()`. This keeps builds independent of transitive standard-library includes, including Clang 23 with libc++.

The durable authority profile provides bounded namespace transactions, producer retry sessions, authenticated multi-voter consensus and completion-based persistence. Managed agents recover immutable local generations and can use polling or publication-aware subscriptions. Startup recovery precedes live admission. See [the authority architecture](docs/AUTHORITY_ARCHITECTURE.md), [managed delivery](docs/DELIVERY_INTEGRATION_ARCHITECTURE.md) and [implementation status](docs/IMPLEMENTATION.md) for APIs, qualification and the remaining relay, incremental publication and fleet work.
