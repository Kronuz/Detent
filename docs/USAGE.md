# Usage guide

Detailed examples for two deployment shapes, then the durable authority profile:

1. An application reading the managed agent's generations through `detent::Reader` (C++, Python or Rust).
2. Shell scripts and other callers reading through `detent get`.

Both read what a managed `detent-agent` publishes; [managed authority delivery](#managed-authority-delivery) provisions it.

Build once before any scenario:

```sh
cmake -B build
cmake --build build -j
```

That produces `build/src/detentd`, `build/src/detent-agent`, `build/src/detent`, the static libraries, the examples, the bindings targets, and the tests.

---

## Scenario 1: an application reading the agent's generations

Use this when a process reads knobs in memory. The process opens the agent's generation directory with the uid the agent runs as, and passes each knob's fallback on every call:

```cpp
#include "detent/reader.hh"
#include <cstdio>
#include <unistd.h>

int main() {
    // A user agent publishes as you; a system agent as `detent` (`_detent` on macOS).
    detent::Reader reader("agent-state", ::getuid());  // throws detent::ReaderError only here

    std::printf("search:max_results = %lld\n",
                (long long)reader.get("search:max_results", 25));
    std::printf("checkout:killswitch_new_path = %s\n",
                reader.eval("checkout:killswitch_new_path", false) ? "true" : "false");
}
```

Before the agent publishes, the program prints the fallbacks:

```text
search:max_results = 25
checkout:killswitch_new_path = false
```

A store that does not exist yet opens, and every lookup returns its fallback with `DETENT_UNAVAILABLE` until a generation appears. Lookups never throw; `reader.lookup(name)` returns the status beside the value (`DETENT_MISSING`, `DETENT_WRONG_TYPE`, `DETENT_UNTRUSTED`, and so on), and `reader.revision(namespace)` the attached revision. A lookup sees a new generation on its own; `reader.refresh()` attaches it earlier, from a thread the caller picks. The reader starts no thread. [`examples/demo.cc`](../examples/demo.cc) is the complete program.

Link `detent::reader`. Python's `detent.Reader(store, publisher_uid, namespaces=())` and Rust's `detent::Reader::open(store, publisher_uid, &[..])` have the same shape over the same C ABI (`include/detent/reader.h`).

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
reader.eval("payments:new_processor", false);                                 // false
reader.eval("payments:new_processor", false, "user-1");                       // false
reader.eval("payments:new_processor", false, "user-42");                      // true
reader.eval("payments:new_processor", false, "user-2", "internal_employees"); // true
```

Evaluation order is:

1. Exact-match `switch_value` override.
2. `rollout_pct` gate, when `hash` is present.
3. The knob's default.

The rollout bucket is stable for the same `(knob_name, hash)` pair across processes and restarts.

Shell reads of a rollout use the same `--hash` and `--switch` parameters; see Scenario 2.

---

## Scenario 2: host-local reads from the shell

Use this when shell scripts or programs without a binding need reads without each one connecting to the authority.

A managed `detent-agent` receives authenticated deliveries from a durable authority and publishes immutable generations on disk; `detent get` maps the newest generation read-only. [Managed authority delivery](#managed-authority-delivery) below provisions `managed.json` and the generation directory. With them in place:

```sh
./build/src/detent get search:max_results --managed managed.json --generation-dir ./agent-state
# 500 (or the latest published value)
./build/src/detent get payments:new_processor --hash user-42 --managed managed.json
./build/src/detent get payments:new_processor --hash user-2 --switch internal_employees --managed managed.json
./build/src/detent-agent --status --managed managed.json
```

`get` starts the agent when no durable generation is present, and waits for the first one. A durable generation stays readable with the agent stopped. `--default VALUE` supplies the fallback for that read: it prints for a knob the generation does not hold, a wrong type, or no generation, but not for a generation that fails verification.

With no `--managed` and no `--generation-dir` (or `managed_path` and `generation_dir` in `agent.json`), both `detent get` and `detent-agent` exit with `no source configured`.

### Persistent agent configuration

Write `agent.json` in the user config directory:

| Platform | User config directory | System fallback |
| --- | --- | --- |
| Linux | `$XDG_CONFIG_HOME/detent`, otherwise `~/.config/detent` | `/etc/detent` |
| FreeBSD | `$XDG_CONFIG_HOME/detent`, otherwise `~/.config/detent` | `/usr/local/etc/detent` |
| macOS | `~/Library/Application Support/Detent` | `/Library/Application Support/Detent` |

An absolute `XDG_CONFIG_HOME` overrides the native user location on macOS too. The first existing `agent.json` wins (user before system); `--config PATH` selects a file explicitly. Config files contain JSON:

```json
{
  "managed_path": "managed.json",
  "generation_dir": "agent-state"
}
```

Then use plain `detent get NAME` from any directory.

### Runtime files

Runtime files (`agent.pid`, `agent.log`) go in `$XDG_RUNTIME_DIR/detent` when an absolute XDG runtime directory is set. Otherwise the agent uses a private `detent-<uid>` directory under the system temporary directory (`TMPDIR` when set, normally `/tmp` on Linux/FreeBSD and a per-user temporary directory on macOS). It creates the runtime directory with mode `0700`. `runtime_dir` in config or `--runtime-dir PATH` overrides it.

Flags override environment variables, which override config. The optional variables are `DETENT_MANAGED_CONFIG`, `DETENT_GENERATION_DIR`, and `DETENT_RUNTIME_DIR`. Startup settings configure a new daemon; stop the running daemon to apply changed settings. Keep the same runtime directory for `get`, `status`, and `stop`.

Startup errors are reported to the caller. Daemon output is saved in `agent.log` in the runtime directory.

---

## Cleaning up

```sh
kill -TERM "$(./build/src/detent-agent --status --managed managed.json | awk '/^running/ {gsub(/[^0-9]/, "", $3); print $3}')"
```

## Where to go next

- [`README.md`](../README.md): architecture, dependencies, bindings, benchmarks, test coverage, and production-readiness notes.
- [`justknobs-implementation-spec.md`](../justknobs-implementation-spec.md): the source specification.

System-wide deployment: see [System services](SERVICES.md) for service accounts, directory ownership, supervision, and logging on Linux, FreeBSD, and macOS.

The shell reader is `detent get NAME`. Both daemons run directly in the foreground, accept `--daemonize` for user background operation, and provide read-only `--status`. Send SIGTERM to stop a user daemon; supervised services use their service manager. The reader launches a sibling `detent-agent` binary, or finds it on `PATH` when invoked by name.

## Durable authority profile

`detentd --authority CONFIG` uses a durable store and a loopback transaction API. It runs in the foreground, accepts `--daemonize` and provides read-only `--status`. See [the authority guide](AUTHORITY_ARCHITECTURE.md) for build sources, credentials, session retries, namespace CAS and explicit migration.

### Managed authority delivery

The managed profile connects the agent to the authenticated authority endpoint and publishes durable immutable generations. Select the managed profile with `detent-agent --managed managed.json --generation-dir ./agent-state`; readers use the public generation directory and never open its credential configuration.

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

A public `agent.json` can select `managed_path` and `generation_dir`. Set all four public identity fields (`generation_cluster`, `generation_group`, `generation_epoch`, `generation_scope`) to bind shell reads and status to the intended authority scope. Their flag equivalents are `--generation-cluster`, `--generation-group`, `--generation-epoch`, and `--generation-scope`; partial identities are rejected. Identity strings use lowercase hexadecimal (32 characters for cluster/group/epoch, 64 for scope). Distribute this public identity separately from the private token.

`detent get NAME --generation-dir ./agent-state` reads the selected durable generation even while the agent is stopped. With a managed profile configured, user reads can auto-start a missing agent; `--system` leaves process startup to the service manager. A knob the generation does not hold is an error unless `--default VALUE` is given. `detent-agent --status --generation-dir ./agent-state` reports durable revision and count independently of the process status.

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

Managed reads default to `reader_validation: "prepared"`. The daemon validates and prepares the immutable generation; applications authenticate its publisher and verify the pages they access. Explicit `reader_validation: "full"` supports flat or separately trusted stores and validates the complete graph on attachment. Missing or malformed prepared metadata never selects full validation automatically.

The trusted `publisher_user` setting accepts an account name or numeric UID. Its default is the effective user for per-user operation, `detent` for Linux/FreeBSD system services, and `_detent` for macOS system services. `--publisher-user` and `--reader-validation` override these settings and have no environment-variable aliases. Numeric UIDs avoid account-name resolution on the read path. Publisher directories require protected ancestry; root-controlled aliases such as macOS `/var` are supported. User-controlled aliases are refused.

Applications read the same prepared path through `detent::Reader`, whose one trust input is the publisher uid passed to it. Keep one reader per process for its lifetime: it holds the mapped pages and the leased generation, and lookups reattach when the agent publishes.

Public `agent.json` can configure managed `detent get` and `detent-agent --status` independently of the daemon's private delivery credentials. Each setting also accepts a flag and environment variable: `reader_image_bytes`, for example, becomes `--reader-image-bytes` and `DETENT_READER_IMAGE_BYTES`. Flags override the environment, which overrides the file.

| Setting | Default | Bound |
| --- | --- | --- |
| `reader_records` | Format maximum | Positive 32-bit record count |
| `reader_namespaces` | Format maximum | Positive 32-bit namespace count |
| `reader_overrides` | 256 | 0–256 per definition |
| `reader_image_bytes` | Format maximum | Explicit settings use positive signed 64-bit extent |
| `reader_string_bytes` | Format maximum | Explicit settings use positive signed 64-bit count |
| `reader_pages` | Format maximum | Positive 64-bit page count |
| `reader_segments` | Format maximum | Positive 64-bit segment count |
| `reader_heap_bytes` | 8 MiB | Positive host allocation width |
| `reader_mapping_bytes` | 512 MiB | Positive host allocation width |
| `reader_owners` | 16,384 | Positive host-width resident ownership count, subject to domain admission |

Prepared reads use format-wide corpus limits and shared process resource budgets unless explicitly configured. Actual allocations and mappings require ledger admission. Explicit settings create command-owned heap, retirement and mapping resources; views close before those resources drain. The retirement runner retains its separate 1 MiB baseline budget. Full validation retains bounded defaults of 500,000 records, 4,096 namespaces, 256 MiB image bytes and 128 MiB strings unless explicitly configured.

A file supplying `publisher_user`, `reader_validation` or any reader-capacity field must be a regular file owned by root or the effective user, without group/other write permission, an extended ACL that adds access rules, or a final symlink. The loader checks the descriptor it reads and rejects a changed file extent. World-readable mode `0644` is allowed when its ACL is safe. On macOS and FreeBSD, nontrivial ACLs are refused; on Linux, the POSIX ACL effective mask is reflected in the group mode bits. Credentials belong in the separate private managed configuration. The bounded legacy configuration path keeps its existing symlink behavior when no reader settings are present.

The image never supplies its own limits. Keep generation identity binding configured, and fund the reader separately when increasing daemon capacity.

Linux configuration trust assumes a filesystem whose POSIX ACL grants are reflected in the mode mask. NFSv4 and rich-ACL mounts are outside this qualification. FreeBSD accepts a successfully inspected trivial ACL, or unsupported ACL retrieval when both descriptor capability queries confirm ACLs are disabled. Enabled capabilities, inspection errors and indeterminate capability results refuse trust. Native coverage includes ACL-disabled UFS and a private POSIX-ACL UFS filesystem; NFS4-enabled UFS and ZFS remain outside this qualification. macOS rejection is covered by the native regression and independent CLI probe.
