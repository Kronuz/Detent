# Quick start

This file walks through four current deployment shapes:

1. One process reading baked defaults only.
2. One `detentd` distributor serving live updates.
3. One `detent-agent` daemon serving host-local non-C++ readers.
4. A three-node `detentd` cluster using Raft consensus.

Build once before any scenario:

```sh
cmake -B build
cmake --build build -j
```

That produces `build/src/detentd`, `build/src/detent-agent`, the static
libraries, the examples, the bindings targets, and the tests.

---

## Scenario 1: static config, no network

Use this when a process only needs baked defaults and only picks up changes on
restart.

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

With no `source_path` and no `source_poll`, the client does one synchronous
startup parse and then serves reads from the published in-memory snapshot.

### Gradual rollouts and per-group overrides

Bool knobs can combine a rollout percentage with named overrides:

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

The rollout bucket is stable for the same `(knob_name, hash_val)` pair across
processes and restarts.

From the shell, `detent-agent get` exposes the same `--hash` and `--switch`
parameters:

```sh
./build/src/detent-agent get payments:new_processor
./build/src/detent-agent get payments:new_processor --hash user-42
./build/src/detent-agent get payments:new_processor --hash user-2 --switch internal_employees
```

---

## Scenario 2: one distributor, live updates

Use this when several processes need live updates without a restart.

Start one distributor:

```sh
./build/src/detentd 8080 /tmp/detent-snapshot.json &
```

Publish the first snapshot:

```sh
curl -s -X PUT localhost:8080/v1/snapshot -d '{
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
curl -s localhost:8080/v1/snapshot
```

Point a client at it:

```cpp
#include "detent/client.hh"
#include "detent_http/http_source.hh"

using namespace std::chrono_literals;

detent::InitOptions client_opts;
client_opts.defaults_path = "defaults.json";
client_opts.source_poll =
    detent_http::make_http_poll_source("127.0.0.1", 8080, "/v1/snapshot", 2s);
client_opts.poll_interval = 200ms;
detent::Client client(std::move(client_opts));
```

`detentd` serves `GET /v1/snapshot` with a cached weak ETag. Pollers send
`If-None-Match`; unchanged state returns `304 Not Modified`.

Inspect the conditional GET behavior:

```sh
curl -sv localhost:8080/v1/snapshot 2>&1 | grep -i '< HTTP\|etag'
etag=$(curl -sD - -o /dev/null localhost:8080/v1/snapshot | awk '/^ETag:/ {print $2}' | tr -d '\r')
curl -sv localhost:8080/v1/snapshot -H "If-None-Match: $etag" 2>&1 | grep -i '< HTTP\|etag'
```

Health endpoint:

```sh
curl -s localhost:8080/v1/health
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
curl -s -X PUT localhost:8080/v1/snapshot -d '{"schema_version":1,"version":2,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":false},"search:max_results":{"type":"int","default":500}}}'
```

Every polling client converges to version 2 within one `poll_interval`.

### Partial updates (PATCH), diffs, and compression

Update just the changed knob:

```sh
curl -s -X PATCH localhost:8080/v1/snapshot -d '{"schema_version":1,"version":3,"knobs":{"checkout:killswitch_new_path":{"type":"bool","default":true}}}'
```

Pollers can ask for the cached diff from their last accepted version:

```sh
curl -s -D - localhost:8080/v1/snapshot -H "X-Detent-Base-Version: 2"
```

When the base version matches, `detentd` replies with:

- `Content-Type: application/merge-patch+json`
- `X-Snapshot-Version: 3`
- `X-Patch-Base-Version: 2`

If the base version is missing, stale, or unknown, `detentd` falls back to the
full snapshot body.

Measured example sizes from the current implementation:

- Full body: 276 bytes
- Cached diff: 138 bytes
- Gzip-compressed full body: 173 bytes

Compression is negotiated with `Accept-Encoding` and cached per accepted
update. Bodies smaller than 256 bytes stay uncompressed.

---

## Scenario 3: host-local reads for non-C++ callers

Use this when local shell scripts or non-C++ programs need fast reads without
each one polling `detentd`.

Start `detent-agent` against the distributor from Scenario 2:

```sh
export DETENT_RUNTIME_DIR=/tmp/detent-run
export DETENT_DEFAULTS_PATH=/tmp/detent-defaults.json
export DETENT_SOURCE_HTTP=127.0.0.1:8080
mkdir -p "$DETENT_RUNTIME_DIR"

# Write the same baked-defaults JSON shape as Scenario 1 to
# /tmp/detent-defaults.json before starting the agent.

./build/src/detent-agent start --daemonize
./build/src/detent-agent status
```

Read knobs:

```sh
./build/src/detent-agent get search:max_results
./build/src/detent-agent get payments:new_processor --hash user-42
./build/src/detent-agent get checkout:new_flow --switch beta_testers
```

Stop it:

```sh
./build/src/detent-agent stop
```

If the daemon is not running, `get` auto-starts it with the same flag and
environment-variable resolution as `start`.

Implementation summary:

- `detent-agent` runs one real `detent::Client`.
- On each accepted update, it serializes the snapshot into a flat shared-memory
  layout.
- `get` maps that file read-only and evaluates directly against the published
  bytes.
- The read path does not talk to the daemon process per request.

Measured throughput on the same development machine:

- `detent_read_bench 4 2000000`: 6.71M ops/sec
- `detent_agent_shm_bench 4 2000000`: 36.2M ops/sec

---

## Scenario 4: a multi-node cluster

Use this when the distributor itself needs replication and failover.

Start three same-host replicas with the exact `--cluster` syntax from
`src/detentd/main.cc`:

```sh
./build/src/detentd 8081 /tmp/jk-a.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 &
./build/src/detentd 8082 /tmp/jk-b.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8082 127.0.0.1:8081 127.0.0.1:8083 &
./build/src/detentd 8083 /tmp/jk-c.json --cluster demo 23864 239.255.0.1 127.0.0.1 127.0.0.1:8083 127.0.0.1:8081 127.0.0.1:8082 &
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

The response still carries `{"accepted_version":1}`. In clustered mode it also
includes `X-Detent-Leader`, which names the current leader.

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

A real `detent::Client` in this setup should use the multi-node
`make_http_poll_source` overload rather than pinning itself to one node:

```cpp
opts.source_poll = detent_http::make_http_poll_source({
    {"127.0.0.1", 8081}, {"127.0.0.1", 8082}, {"127.0.0.1", 8083},
});
```

It starts on a random one of the three (so a fleet of client processes
spreads across the cluster instead of piling onto node one) and fails over
to another if its current node stops answering -- reads don't need the
leader specifically, every node applies the identical committed sequence.

Current automated cluster coverage in `tests/detentd/test_raft_cluster.cc`
exercises:

- leader election,
- writes submitted through a follower,
- PATCH replication,
- leader failover with a surviving majority,
- restart safety and rejoin,
- dead-follower liveness (a permanently-gone follower doesn't block the
  surviving majority),
- a node's own write right after restarting isn't confused with one of
  its pre-crash writes.

`tests/e2e/` complements this with a slower harness that spawns real
`detentd` subprocesses and kills them with real signals -- run it after
any change to `src/detentd/` or the vendored Raft wiring; see
`tests/e2e/README.md`.

`src/detentd/CLUSTERING.md` is the full clustering design reference.

## Cleaning up

```sh
./build/src/detent-agent stop
kill %1 %2 %3
```

## Where to go next

- [`README.md`](README.md): architecture, dependencies, bindings, benchmarks,
  test coverage, and production-readiness notes.
- [`justknobs-implementation-spec.md`](justknobs-implementation-spec.md): the
  source specification.
- `src/detentd/CLUSTERING.md`: the authoritative clustering design.
