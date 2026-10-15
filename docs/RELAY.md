# Durable relay

`detent-relay` consumes authenticated authority subscriptions, selects durable local generations and distributes shared binary snapshots and deltas to agents. Applications keep reading their local agent generations. Relay hosting has Stage 5B component acceptance; the remaining immutable-publication and sustained fleet gates are tracked in [the architecture plan](RELAY_ARCHITECTURE.md).

```text
Authority → Relay → Agents → Local application readers
            durable   durable
            cache     generations
```

Build the optional daemon with `-DDETENT_BUILD_RELAY=ON`. A relay-only build can disable `DETENT_BUILD_DISTRIBUTOR` and `DETENT_BUILD_AUTHORITY`; it uses the standalone completion module and does not link authority state or consensus. Normal managed agents require `DETENT_BUILD_MANAGED_AGENT=ON`.

## Private configuration

The default configuration is `relay.json` in the user configuration directory, or the system configuration directory with `--system`. `--config PATH` selects another file. Keep the host and upstream credential files owned by root or the service account, with mode `0600` or controlled group-readable `0640`. They must be regular files with one link; symlinks are rejected.

An upstream source file uses the [managed delivery configuration](USAGE.md#managed-authority-delivery):

```json
{
  "cluster": "63000000000000000000000000000000",
  "group": "67000000000000000000000000000000",
  "epoch": "65000000000000000000000000000000",
  "principal": "application-reader",
  "policy_epoch": 1,
  "namespaces": ["app"],
  "token": "replace-with-a-private-upstream-token",
  "address": "127.0.0.1",
  "port": 8317,
  "hostname": "localhost",
  "ca": "tls/ca.pem"
}
```

The principal, policy and namespaces must match the authority credential. The scope identity is computed from them. Relay sources always use subscription delivery: omitting `delivery_mode` selects subscriptions, while an explicit `"poll"` is rejected. Relative certificate paths are resolved against the source file.

A corresponding `relay.json` is:

```json
{
  "address": "127.0.0.1",
  "port": 8318,
  "hostname": "localhost",
  "certificate": "tls/relay.pem",
  "private_key": "tls/relay.key",
  "ca": "tls/ca.pem",
  "bundles": [
    {
      "source": "upstream.json",
      "directory": "relay-generations/app",
      "mode": "fresh"
    }
  ],
  "credentials": [
    {
      "token": "replace-with-a-private-downstream-token",
      "principal": "application-reader",
      "policy_epoch": 1,
      "bundles": [0]
    }
  ]
}
```

Tokens must contain 32 to 128 printable bytes. The listener requires TLS; `hostname` is the certificate name used by `--status`, and must match its SAN. Paths in the host configuration are relative to that file. Bundle indices refer to the fixed configured list. Each downstream credential’s principal and policy must match every bundle it grants. Exact identity, profile and namespace routing prevents a request from creating a bundle or widening its scope.

Use `"mode": "fresh"` only to provision a new empty store, then change it to `"existing"` or omit it before restarting. Existing stores require their selected identity to match the configured source, or an explicit trusted `reset_from` transition. Relay directories must be owned by the service account and private (`0700`); relay artifacts use `0600`, including system operation. They are separate from publicly readable agent generations.

## Run and inspect

```sh
detent-relay --config relay.json
detent-relay --config relay.json --status
```

The daemon runs in the foreground. `--daemonize` provides unmanaged user operation; supervised services use foreground operation and service-manager logging. `--system --daemonize` and `--status --daemonize` are rejected. `--runtime-dir PATH`, `--log-level`, `-v` and `--color` use the same conventions as the other daemons. The PID filename is `relay.pid`; use separate runtime directories for distinct relay instances.

`--status` queries the HTTPS health endpoint and never starts the daemon. The health snapshot reports readiness, configured heap capacity and occupancy, request/transfer counts, selected bundle revisions, cache gaps, allocation refusals and mapping use. It contains no bearer tokens or knob definitions. Readiness requires recovery and fresh upstream authentication for all configured bundles; the ready health snapshot precedes the startup announcement. SIGTERM stops intake, drains accepted originals and coordinators, then releases storage and joins workers. Failure to allocate the initial Asio coordinator run group uses the existing fail-stop policy; it cannot promise graceful drainage for coordinators whose run original was never admitted. A service manager restarts the process using the last durable selection.

An agent consuming the relay uses the same managed source configuration, with the relay endpoint, downstream token and explicit `"delivery_mode":"subscription"`. Keep the source principal, policy and namespaces unchanged so its scope matches the bundle. Applications continue using their public reader configuration and local defaults.

## Capacity and continuity

The host accepts up to 256 configured bundles and 10,000 credential entries within the 1 MiB private configuration limit. Duplicate configured identities, canonical durable paths, opened directory identities and credential tokens are rejected. Each bundle has separate publication and cache heap ceilings; the root budget must cover every child reservation plus control and baseline capacity. Mapping and disk limits remain separate. Startup refuses configurations that cannot admit retained metadata.

Defaults reserve 256 MiB of publication heap and 64 MiB of cache per bundle, 64 MiB of control heap and 16 MiB of baseline heap. Open, Next and Close have independent count and heap lanes. The runtime has 1,024 sessions, 640 admitted polls, 512 bootstrap transfers and 128 catch-up transfers. These are admission ceilings, not measured sustainable fleet throughput. TLS/framework allocations and process RSS are separate from admitted component capacity.


The optional `connections` field bounds accepted connections (default 1,024, range 1–65,536). `service_limits` configures independent admission lanes:

```json
{
  "connections": 16384,
  "service_limits": {
    "opens": 16,
    "polls": 10000,
    "closes": 16,
    "sessions": 10000,
    "per_principal": 10000,
    "bootstrap_transfers": 512,
    "catchup_transfers": 128,
    "idle_ms": 60000,
    "maximum_ms": 600000
  }
}
```

Count limits must be 1–65,536. Each transfer limit must fit within `polls`, and `per_principal` must fit within `sessions`; omitted dependent limits shrink to their enclosing limit. Session lifetimes must be 1–86,400,000 milliseconds, with `maximum_ms` at least `idle_ms`. Raising counts requires sufficient heap and operating-system descriptors; these values establish admission ceilings and do not establish sustainable throughput.

Cache allocation pressure preserves durable publication and produces a continuity gap. The host rebuilds a current-head snapshot with bounded work and retry backoff. Source lag returns HTTP 503 without closing the session; a gap requires a snapshot. Requests and offered objects remain retained until authentic transport settlement, including timeout and Close. See [qualification evidence](RELAY_ARCHITECTURE.md) for measured state sizes and the remaining network, platform and fleet gates.

## System services

Install the matching template from `packaging/systemd`, `packaging/launchd` or `packaging/rc.d`. Use a private persistent parent at `/var/lib/detent-relay` on Linux or `/var/db/detent-relay` on macOS and FreeBSD. Configure each bundle below it. The Linux unit and FreeBSD script provision that parent; provision it manually for launchd with owner `_detent` and mode `0700`. Provision `/var/run/detent/relay` with service ownership and mode `0755` for launchd.

The templates use a relay runtime subdirectory and keep logs with the service manager. Create private credential/TLS files and provision fresh stores before enabling automatic restarts, then select existing-store mode. Follow [system services](SERVICES.md) for service accounts and installation. Config and grant changes require a restart. For the Linux unit, set each bundle directory to an absolute path such as `/var/lib/detent-relay/app`; the relative example above resolves beside the configuration file and is not writable under `/etc` with the unit’s filesystem protection. On macOS and FreeBSD, use `/var/db/detent-relay/app`. Keep durable state outside runtime directories, which are cleared at boot.

## Shared disk scheduling

Each configured bundle has one pre-admitted completion port. A bounded owner FIFO shares one native completion queue and driver; participant count does not add disk workers. Each grant executes one current primitive and retains its operation through authentic settlement. Queued cancellation can finish the operation or change its request to cleanup before submission. The owner keeps processing network and control work while completion is pending. This serial disk lane bounds resources; simultaneous native submission is a separate future optimization requiring measurements.

Health includes queued/peak disk participants, primitive grants, maximum queue wait in nanoseconds and separate native/fallback submission counts. Queue wait is distinct from the driver’s submitted-to-reaped latency. These are owner snapshots, not whole-process RSS measurements.

Upstream subscription failures use independently seeded exponential reconnect backoff with equal jitter. `poll_interval_ms` sets the initial ceiling; `reconnect_maximum_ms` defaults to 30,000 ms, or the poll interval when larger. Authenticated idle responses and durable publication reset the schedule. Shutdown cancels retry waits through the coordinator owner.

Descriptor limits must cover accepted TLS connections, upstream connections, durable files, mappings and reactor infrastructure. Connection admission bounds application ownership; it does not reserve operating-system descriptors. The 500-client network qualification runs clients and servers in one process and requires a soft descriptor limit of at least 4096. Its fixture raises the soft limit only when the hard limit permits it.
