# Detent (C++)

Detent is a C++ runtime-configuration library for namespaced boolean and integer knobs, plus a durable authority daemon (`detentd`) and a host-local managed agent (`detent-agent`). It implements the model in [`justknobs-implementation-spec.md`](justknobs-implementation-spec.md) and adds authenticated transactional publishing, durable Raft consensus, and incremental delivery to hosts.

**New here?** [`QUICKSTART.md`](QUICKSTART.md) covers numeric settings, feature flags, gradual rollouts, group overrides, and per-read fallbacks. The [usage guide](docs/USAGE.md) covers network distribution, configuration, and library examples.

## What this is

- Applications read through `libdetent_reader`, one reader core behind a C ABI (`include/detent/reader.h`). C++ wraps it as `detent::Reader`; the Python and Rust bindings wrap the same ABI. A lookup maps the agent's generation read-only, never touches the network, and never allocates once warm.
- Every lookup takes the caller's fallback and returns it on any error: a missing knob, a wrong type, a store with no generation yet. `lookup()` also reports the status. Defaults live in each call's fallback.
- Bool knobs support exact-match group overrides and stable SHA-256 percentage rollouts. Integer knobs do not support per-group overrides in v1.
- `detentd` is the durable authority: bounded namespace transactions, publisher credentials, and Raft consensus over mutual TLS with a journal on each voter.
- `detent-agent` receives authenticated deliveries from the authority and publishes immutable local generations, which `detent get` maps read-only for shell scripts and other non-C++ callers.

## How Detent fits together

![Producer, authority, relay, agent and client](docs/diagrams/actors-overview.png)

A knob change moves through five actors:

- **Producer**: your service, writing changes as namespaced transactions with `POST /v2/transactions` over HTTPS.
- **Authority**: `detentd --authority authority.json` on three machines, one voter each. The voters replicate with Raft over mutual TLS and keep the journal on their own disks. This is the source of truth.
- **Relay** (optional): `detent-relay`, subscribed to the authority, keeps a durable copy per bundle and serves snapshots and deltas to many agents.
- **Agent**: `detent-agent`, one per host. It receives snapshots and deltas and publishes immutable local generations on disk.
- **Client**: your application, through `detent::Reader` (or the Python and Rust `Reader`), mapping the agent's selected generation and reading in memory. `detent get` is the command-line client.

This is the managed profile, the topology the [production contract](docs/PRODUCTION_CONTRACT.md) targets. [Detent's actors](docs/ACTORS.md) says what each actor does, how, when and where, including ports, files, build options and restart behavior.

## Using it in your own application

### 1. Pull it in (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(detent
  GIT_REPOSITORY https://github.com/Kronuz/Detent.git
  GIT_TAG        main)
FetchContent_MakeAvailable(detent)

target_link_libraries(my_app PRIVATE detent::reader)
```

For reproducible builds, replace `main` with the commit you have qualified. A local checkout can also be included with `add_subdirectory()`.

### 2. Open a `Reader` and read knobs

```cpp
#include "detent/reader.hh"

// The agent's generation directory, and the uid the agent runs as.
detent::Reader reader(generation_dir, agent_uid);

bool killswitch = reader.eval("checkout:killswitch_new_path", false);
std::int64_t max_results = reader.get("search:max_results", 25);
bool enabled = reader.eval("payments:new_processor", false, user_id, user_group);

auto result = reader.lookup("search:max_results");
if (!result)
    log(detent_status_name(result.status));
```

Open one `Reader` per process and share it; lookups are thread-safe. Only the constructor throws (`detent::ReaderError`, whose `status()` says why); every lookup returns its fallback on any error. A store that does not exist yet opens, and lookups return the fallback with `DETENT_UNAVAILABLE` until the agent publishes. An optional third argument limits the handle to a list of namespaces. `refresh()` attaches a newer generation now instead of on the next lookup, and `revision(namespace)` reports the attached revision. The worked example is [`examples/demo.cc`](examples/demo.cc).

### 3. Run the authority

Start a voter with `detentd --authority authority.json` and publish with `POST /v2/transactions`; the [durable authority guide](website/src/content/docs/guides/durable-authority.md) walks through the configuration and a first transaction.

### 4. Testing

Tests publish real generations with `detent::testing::GenerationWriter` (`include/detent_testing/generation_writer.hh`, target `detent::testing_store`, never installed) and read them through a `Reader`, the same path production takes:

```cpp
#include "detent_testing/generation_writer.hh"

detent::testing::GenerationWriter writer(store);  // creates the store directory (0700)
writer.publish_json(R"({"schema_version":1,"knobs":{"search:max_results":{"type":"int","default":3}}})");
detent::Reader reader(store.string(), ::getuid());
```

Directory trust refuses a store with a group- or world-writable ancestor, so keep test stores under the build tree, not `/tmp`.

## Scope vs. the spec

This repository implements the spec's client model and adds a concrete distribution layer:

- `detent`: the knob model, schema validation and rollout hashing.
- `libdetent_reader`: the reader core and its C ABI, wrapped by `detent::Reader`.
- `detentd`: the durable authority daemon.
- `detent-agent`: host-local managed agent that publishes immutable generations for non-C++ readers.
- Python and Rust bindings over the reader core's C ABI.

The wire format remains `schema_version: 1`. Full snapshots, PATCH payloads, and diff responses all validate through the same schema machinery.

## Dependency graph

```text
detent
  |-- atomic-shared-ptr
  |-- sha256 (+ endian)
  |-- char-classify
  |-- strict-stox
  `-- random

detent-agent (exe)
  |-- detent::delivery_config   # managed delivery over TLS
  `-- detent
```

`detent` has no networking dependencies. `detentd` pulls the HTTP framework plus `consensus` for durable Raft. `detent-agent` links the managed delivery libraries; `detent get` links only the local reader.

## Dependencies

All dependencies are fetched with CMake `FetchContent`.

| Library | Role |
|---|---|
| [`atomic-shared-ptr`](https://github.com/Kronuz/atomic-shared-ptr) | Snapshot publication |
| [`sha256`](https://github.com/Kronuz/sha256) (+ `endian`) | Stable rollout-bucket hash |
| [`char-classify`](https://github.com/Kronuz/char-classify) | Knob-name validation |
| [`strict-stox`](https://github.com/Kronuz/strict-stox) | Strict integer parsing |
| [`random`](https://github.com/Kronuz/random) | Full-jitter reconnect backoff |
| [`compressors`](https://github.com/Kronuz/compressors) | zstd/gzip: distribution frame codecs |
| [RapidJSON](https://github.com/Tencent/rapidjson) | Strict JSON parsing |
| [`http`](https://github.com/Kronuz/http) (+ `reactor`, `http-parser`, `radix-router`) | `detentd`'s HTTP service |
| [`consensus`](https://github.com/Kronuz/consensus) | `detentd`'s durable Raft, with journal and completion I/O |
| standalone Asio | Pulled transitively by `http` and `consensus` |

RapidJSON runs in strict mode here. Unknown fields and malformed snapshots are rejected rather than partially applied. It is pinned to upstream commit `24b5e7a8b27f42fa16b96fc70aade9106cf7102f`; the older 1.1.0 release contains a `GenericStringRef` assignment operator that fails to compile with Clang 23.

## Layout

```text
include/
  detent/              Model, schema and the reader API (reader.h, reader.hh)
  detent_distribution/ Binary protocol, storage and shared sessions
  detent_distribution_http/ Optional server grammar and owned replies
  detent_relay/        Durable cache and session adapters
  detent_local/        Immutable views and durable generations
  detent_delivery/     TLS client and publication coordinator
  detent_transport/    TLS configuration

src/
  detent/              Model and schema implementation
  detent_reader/       Reader core (libdetent_reader)
  detentd/             Authority daemon host
  detent_agent/        Agent host and managed configuration
  detent_authority/    Durable authority, transactions and distribution
  detent_distribution/ Protocol and session implementation
  detent_distribution_http/ Shared server parsing and reply pumping
  detent_relay/        Durable relay core
  detent_local/        Image building and persistence
  detent_delivery/     Session transport and coordinator
  detent_cli/          Shared configuration, logging and daemon lifecycle

tests/                 Matching component suites
examples/
benchmarks/
diagrams/
bindings/
```

## What the source modules build

The model and the reader have no networking dependency. Binary distribution, immutable local views, durable publication and managed TLS delivery are separate targets, so consumers can select the source and storage profile they need. Authority implementation targets remain internal to the daemon and its tests.

Managed agents and relay bundle sources write `"image_format": "paged-v2"` by default, for incremental immutable local publication; `"flat-v1"` selects one self-contained image. Both formats remain readable by the current SDK, and existing flat heads migrate before paged updates. See [writer preference and maintenance](docs/LOCAL_IMAGE_FORMAT.md#writer-preference-and-maintenance) for compaction controls, disk coexistence and reader compatibility.

| Module | Main target | Purpose |
| --- | --- | --- |
| Model | `detent::detent` | Knob model, JSON schema and rollout hash |
| Reader | `detent::reader` | `libdetent_reader`: lookups over mapped generations behind a C ABI |
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

## Binaries and their dependencies

| Binary | Built from | Links | What it is |
|---|---|---|---|
| `detent_demo` | `examples/demo.cc` | `detent::reader` | Opens a `Reader` over an agent's store and reads with fallbacks |
| `detentd` | `src/detentd/main.cc` | `detent_authority_service` -> authority, `http::http`, `consensus::consensus` | Durable authority daemon |
| `detent-relay` | `src/detent_relay/main.cc` | Relay host, private configuration and bounded TLS distribution | Optional durable fan-out daemon; Stage 5B qualification |
| `detent-agent` | `src/detent_agent/main.cc` | CLI and managed delivery libraries | Host-local managed agent |
| `detent` | `src/detent_agent/main.cc` | CLI and local reader libraries | Host-local `get` reader |
| `test_*` (`tests/detent/`, `tests/detent_reader/`) | `tests/detent/*.cc`, `tests/detent_reader/*.cc` | `detent`, `detent::reader` | Model and reader correctness suites |

## Build

The C++20 standard library must provide `std::jthread` and stop tokens; configuration checks this before building. libc++ 20 and newer expose these normally. FreeBSD 14.4 with base libc++ 19 requires an explicit `-DCMAKE_CXX_FLAGS=-fexperimental-library` when configuring; this opts into libc++'s experimental API/ABI and must also be used by consumers of the public C++ headers. Prefer a stable implementation when available. See [LLVM 19 documentation](https://releases.llvm.org/19.1.0/projects/libcxx/docs/UsingLibcxx.html) and [LLVM 20 release notes](https://libcxx.llvm.org/ReleaseNotes/20.html).

The authority integration tests require Python 3, Bash, curl, and the OpenSSL command-line tool on `PATH`. Bash is not part of FreeBSD's base system; install it before running the tests. These tools are test dependencies and are not required by the running daemons.

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/examples/detent_demo STORE_DIR AGENT_UID
```

`DETENT_BUILD_AUTHORITY` defaults to `ON`. Set `-DDETENT_BUILD_AUTHORITY=OFF` to skip `detentd` and its HTTP and consensus dependency chain. `DETENT_BUILD_PYTHON` and `DETENT_BUILD_RUST` default to `OFF`.

### Binary size

Measured on macOS/arm64 for `detent-agent`:

| Build | Size |
|---|---|
| Unset `CMAKE_BUILD_TYPE` | 1.15 MB |
| `Release` | 295 KB |
| `Release` + `strip -x` | 242 KB |

Most of the difference comes from RapidJSON and ordinary C++ runtime support.

### Running detentd

```sh
./build/src/detentd --authority authority.json
```

`detentd` listens on port `8317` unless `--port` says otherwise, and serves the authority named by `--authority` (or the `authority` field of `detentd.json`). `detentd.json` lives in the same platform config directory as `agent.json` and accepts `authority`, `port`, `reactors`, `runtime_dir` and the logging fields; relative paths are resolved against the config file. `--config PATH` selects another file. CLI arguments override `DETENTD_*` environment variables, which override config. `detentd --status` reports health over plain HTTP, or over HTTPS when the authority's publisher uses TLS. See [the durable authority guide](website/src/content/docs/guides/durable-authority.md) and [the `detentd` reference](website/src/content/docs/reference/cli/detentd.md).

### Running detent-agent

With a managed configuration (see [managed authority delivery](docs/USAGE.md#managed-authority-delivery)) and a running authority:

```sh
./build/src/detent get search:max_results --managed managed.json
./build/src/detent-agent --status --managed managed.json
kill -TERM "$(./build/src/detent-agent --status --managed managed.json | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

`get` starts the agent when no durable generation is present. With neither `--managed` nor `--generation-dir` (or `managed_path` and `generation_dir` in `agent.json`), `detent get` and `detent-agent` exit with `no source configured`. For persistent settings, put `agent.json` in the platform's user config directory (`~/.config/detent` on Linux/FreeBSD, `~/Library/Application Support/Detent` on macOS):

```json
{"managed_path": "managed.json", "generation_dir": "agent-state"}
```

`--config PATH` selects another config file. `--default VALUE` provides the fallback for that read.

Runtime files use `$XDG_RUNTIME_DIR/detent`, falling back to a private `detent-<uid>` directory under the system temporary directory. `runtime_dir`, `--runtime-dir`, or `DETENT_RUNTIME_DIR` selects another directory. Stop the agent to apply changed startup settings. See [the usage guide](docs/USAGE.md#persistent-agent-configuration) for system config locations and precedence.

## Language bindings

Enable bindings explicitly:

```sh
cmake -B build -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON
cmake --build build -j
```

Both bindings mirror `detent::Reader` over the same C ABI ([design](docs/design/client-local-source.md) D1-D3): only opening raises (`ReaderError`), every lookup returns its fallback on any error, and the handle closes when the object ends, with no `close()`.

- **Python** (`bindings/python/`): a C extension, `detent.Reader(store, publisher_uid, namespaces=())`, with `Status` and `Lookup`.
- **Rust** (`bindings/rust/`): `detent::Reader::open(store, publisher_uid, &[..])` returning `Result<Reader, ReaderError>`, with `Status` and `Value`. The crate links the CMake-built shared `libdetent_reader` and compiles no C++.

Python example:

```python
import detent

reader = detent.Reader(generation_dir, agent_uid)
reader.eval("payments:new_processor", False, hash="user-42")
reader.eval("checkout:new_flow", False, switch_value="beta_testers")
reader.get("search:max_results", 25)
reader.lookup("search:max_results").status
```

Rust example:

```rust
let reader = detent::Reader::open(generation_dir, agent_uid, &[])?;
reader.eval("payments:new_processor", false, Some("user-42"), None);
reader.eval("checkout:new_flow", false, None, Some("beta_testers"));
reader.get("search:max_results", 25);
reader.lookup("search:max_results", None, None);
```

## Testing

CTest registers component and process tests for the enabled build profiles. Use `ctest --test-dir build -N` to list the exact set for your configuration.

1. Model and reader tests cover schema validation, rollout vectors, evaluation, generation switches, fork and syscall-free lookups, and one scenario table (`bindings/tests/scenarios.json`) that C++, Python and Rust all run.
2. Authority tests cover transactions, durable storage, authenticated consensus, restart and failure recovery, bounded distribution, subscription admission and large-state idling.
3. Protocol, local-view and delivery tests cover binary validation, mapped generations, original IO settlement, publication reconciliation and exact source feedback.
4. Process fixtures start real daemons, exercise HTTP and local readers, and stop or kill tracked processes.

## Benchmarks

Benchmarks in `benchmarks/` build by default. Measured output on one 14-core arm64 development machine:

| Benchmark | What it measures | Command | Result |
|---|---|---|---|
| `detent_hash_bench` | `stable_bucket()` only | `detent_hash_bench 5000000` | 5.05M calls/sec (198.2 ns/call) |

`reader_bench` measures warm lookups through `libdetent_reader`; its usage is in the header of `benchmarks/reader_bench.cc`.

### Security boundary

`detentd` authenticates publishers with bearer tokens, voters with mutual TLS and a certificate pin, and bounds every admission layer; see [Security](website/src/content/docs/operations/security.md). Host-local reads trust the host.

### Consistency model

Within one authority group, transactions commit in a single replicated log and the authority assigns revisions.

## Production readiness: honest checklist

| Area | Status | Detail |
|---|---|---|
| Correctness (reader) | **Proven** | Golden hash vectors, the reader core, generation switches under threads, and the shared scenario table in C++, Python and Rust |
| Correctness (host-local agent) | **Proven** | Managed-process and system suites: delivery, restart, failover and reads |
| Linux sanitizer coverage | **Proven on Linux** | ASan/UBSan and TSan run clean across the full suite (multiple stress repetitions) |
| Real network conditions | **Not done** | Current measurements are loopback-only |
| Soak/long-run stability | **Not done** | No multi-hour or multi-day validation documented |


System-wide deployment: see [System services](docs/SERVICES.md) for service accounts, directory ownership, supervision, and logging on Linux, FreeBSD, and macOS.

The shell reader is `detent get NAME`. Both daemons run directly in the foreground, accept `--daemonize` for user background operation, and provide read-only `--status`. Send SIGTERM to stop a user daemon; supervised services use their service manager. The reader launches a sibling `detent-agent` binary, or finds it on `PATH` when invoked by name.

### Rollouts by group

Boolean overrides may be `true` or `false`, or an object containing only `rollout_pct` (an integer from 0 to 100):

```json
"overrides": {"employees": true, "beta": {"rollout_pct": 50}, "production": {"rollout_pct": 5}}
```

A matching group rollout replaces the knob-wide rollout percentage and uses the same hash bucket. Without a hash, it returns the knob's default. Unknown groups use the knob-wide rollout. Boolean overrides still force their value. The behavior is shared by the reader core, the language bindings, and `detent get`. PATCH replaces a complete knob definition, including all its groups.

Daemon diagnostics: see [Logging](docs/LOGGING.md) for verbosity, terminal colors, service configuration, and measured footprint. The logger is linked only into daemon executables.

Large-state architecture and implementation gates: see the [Validated Architectural Blueprint](docs/VALIDATED_ARCHITECTURE.md) and [Scaling Detent](docs/SCALING.md), including the measured 100,000/500,000-knob baseline and the Xapiand compatibility plan. The blueprint is a source-reviewed, conditional architecture approval; consensus correctness, durability, and fleet capacity still require the specified validation gates.

The [production operating contract](docs/PRODUCTION_CONTRACT.md) states topology, retry retention, staleness, restore and upgrade boundaries. Current work and reproduced regression evidence are tracked in the [implementation record](docs/IMPLEMENTATION.md).

The durable authority profile provides bounded namespace transactions, producer retry sessions, authenticated multi-voter consensus and completion-based persistence. Managed agents recover immutable local generations and can use polling or publication-aware subscriptions. Startup recovery precedes live admission. See [the authority architecture](docs/AUTHORITY_ARCHITECTURE.md), [managed delivery](docs/DELIVERY_INTEGRATION_ARCHITECTURE.md) and [implementation status](docs/IMPLEMENTATION.md) for APIs, qualification and the remaining relay, incremental publication and fleet work.
