# Changelog

## Unreleased

### One knob type

- One knob type: every knob is a signed 64-bit integer with a default, group overrides (a forced
  value or a percentage) and an optional rollout serving `rollout_value`; a flag is default 0,
  rollout value 1. `BoolDef`, `IntDef`, the JSON `type` field, `eval` and `eval_or_panic` are
  gone; reads are `get`, `get_as<T>` (`get_as<bool>` takes 0 and 1) and `get_or_panic`. A hashed
  caller outside a percentage gets `default` (D68). JSON, wire, paged body (varint, 20% fewer page
  bytes than the two-type body), flat image and reader metadata change with no compatibility.

### IPv6

- Every address Detent takes or listens on works over IPv6 as over IPv4: voter hosts, `publisher_bind`,
  the relay's `address`, and the `address` of the agent's and delivery client's upstreams take an IPv6
  literal (`"::1"` in JSON). A listener on `::` serves IPv4 and IPv6
  clients on every OS, and a named local voter listens on `::` whenever the host has IPv6 (else
  `0.0.0.0`). The consensus pin moves to 3d988d1 (dual-stack `::` listeners, staggered dial). Tests:
  a three-voter authority, relay and agent over `::1` deliver a generation; a relay and an authority
  on `::` answer IPv4 and IPv6 clients.

### detentd readiness

- `detentd --status` and `/v1/health` report `"ready":true` only once the node has a leader: it leads,
  or it follows a known leader. `detentd --daemonize` waits for that when the node alone is a quorum
  (the single-voter configuration, the quick-start case), so the first `/v2/open` after it succeeds
  without a retry and the quick-start no longer loops on it. With a fixed voter set, `--daemonize`
  returns once the listener is up, as before, and `--status` reports `"ready":false` until a leader
  exists. A follower stays ready only while it hears from its leader within `election_max` (2 s); a
  node that loses its leader and its quorum reports `"ready":false` within that window plus a health
  interval. A `--daemonize` stopped by SIGTERM, or whose runtime fails, during the wait exits non-zero
  with the cause instead of reporting ready or timing out after 30 s.

### Hash once per tier (namespace S4)

- A tier no longer hashes its own build twice more. The plan builders compared every pack checksum and
  page digest of the image the encoder had just written, and the recovery after a publication hashed and
  walked the generation again; both now trust the build (the walk that derives the live counts stays, the
  digest comparisons go). The recovery opens a freshly published generation through `.read` and `.live`
  like a restart. Cold bootstrap, 10K knobs: the agent hashes 698,201 bytes of pages and packs, its own
  build's, instead of 2,094,603; it re-hashes 0 bytes instead of 1,396,402. No format or wire byte changes;
  frame SHA-256 is unchanged.
- Hash counters per tier: `paged::built_hash_counts()` and `paged::checked_hash_counts()` (disjoint, each
  only growing), `distribution::frame_hash_counts()` and
  `delivery::HashStats`, in `CoordinatorStatus::hash`, the relay's `/v1/health` bundle fields
  `hash_*_bytes` and the agent's log line after each publication.

### Trusted restart (namespace S3)

- The agent's cold start no longer hashes or walks the packs. Each paged generation now carries
  `generation-<id>.live`, written with its other files before `selected` is replaced; a restart
  reads it with `.read`, maps the packs without reading them, and checks each page against its
  parent reference the first time any read reaches it. Restart over 100K knobs: 0.67 s to 0.12 s
  (smoke, one host; `docs/perf/trusted-restart.md`).
- **A store written before this change is refused, and the refusal is fatal.** It has no `.live`,
  so the agent's cold start throws, the coordinator ends, and the agent does not start; the
  operator must bootstrap a fresh store. A damaged or missing `.live` or `.read` refuses the
  same way. There is no fallback path.
- A paced scrub task hashes the selected generation's packs against their owner records at
  `scrub_bytes_per_second` (managed configuration; default 524,288; 0 disables). A damaged pack
  is reported in the coordinator status, the agent's log and the relay's `/v1/health` bundles
  (`scrub_passes`, `scrub_damaged_packs`, `scrub_source_failure`).
- Removed the coordinator's "republish a missing `.read`" path and `PagedPlanBuilder::Republish`;
  `migrate_flat_head` now acts only on a flat (layout 1) base.

### Repair of a damaged generation (namespace S3b)

- A scrub report of a damaged pack, or a first-touch failure in the update path, makes the
  coordinator ask upstream for a Replace and publish it as a repair: a fresh generation at the
  selected cursor or later, built without the equality check (so unchanged content still
  publishes), recovered without reading the damaged generation, and sharing no pack with it.
  Reads of the damaged pages fail with `SourceFailure` until it is selected; nothing is fenced.
  The agent no longer needs a bootstrap to recover from a damaged pack, and a delta that crosses a
  damaged page no longer fails every round. `CoordinatorStatus::repair` reports `requested`,
  `completed` and `pending`.
- The collector removes a pack whose checksum differs from its owner record once no generation
  reaches it (the owner record, the file's ownership and its identity are still proved); the
  coordinator asks for a collection when a repair completes.
- The scrub hashes the pack files (pread, uncached: `F_NOCACHE` on macOS, `posix_fadvise` on
  Linux) instead of the agent's private mapping, so damage that lands after the packs were mapped
  is found; a pack that is missing, resized or unreadable is reported too. A mismatch withdraws
  the pack's verified-page bits, so later reads verify their pages again.
- The relay's `Bundle` takes a repair at its own cursor (`PublicationSink::selected_repaired`)
  without rebuilding its cache.

### Legacy removal (wave 2)

- Removed the `detent_publish` example.
- Removed the `detent-authority-admin` import/export tool, its
  `test_authority_migration` test and its website pages. The offline
  recovery runtime that `test_authority_cluster` used is now a test helper.
- Removed the classic distributor and classic Raft: `detentd.cc`, `raft_delegate`,
  `CLUSTERING.md`, `tests/detentd`, `detentd_fanout_bench`, the
  `publication_bench` probe and `test_agent_http`. `detentd` is the durable
  authority daemon only: `--authority CONFIG` is required, and `--persist-path`,
  `--cluster`, `--raft-timing` and the positional `<port> <persist_path>` form
  are gone. `detentd --status` probes the authority's health endpoint over plain
  HTTP, or over HTTPS when the publisher uses TLS.
- `DETENT_BUILD_DISTRIBUTOR` is removed. `DETENT_BUILD_AUTHORITY` is on by
  default and builds `detentd`, the authority libraries that need consensus,
  and their tests and benchmarks.
- The `cluster`, `generation_cache` and `http_log` pins are removed from
  `cmake/dependencies.json`.
- `kLoopbackHost` moved from `detent_http/address.hh` to
  `detent_cli::kLoopbackHost` in `src/detent_cli/config.hh`.

### Legacy removal (wave 2, step G)

- Removed the shared-memory agent and the unmanaged `detent-agent` path:
  `agent.cc`, `shm_writer`, `shm_reader`, `shm_format.hh`, the `detent_agent_lib`
  target, `snapshot.shm`, `test_agent_basic`, `test_snapshot_canonical`,
  `test_shm_concurrency_stress`, `test_system_agent` and the
  `detent_agent_shm_bench` benchmark. `detent_scale_bench` no longer reports the
  shared-memory image.
- `detent-agent` runs the managed profile only, and `detent get` reads durable
  generations only. With no source configured (no `--managed`, `managed_path`,
  `--generation-dir` or `generation_dir`) both exit 1 with `no source configured`,
  whatever `--default` or `--defaults` say.
- `--source-path`, `--poll-interval-ms`, `DETENT_SOURCE_PATH`,
  `DETENT_POLL_INTERVAL_MS` and the `source_path` and `poll_interval_ms` fields of
  `agent.json` are gone; each is now an unknown argument or field. `--defaults`
  stays a `detent get` option (it is no longer forwarded to a started agent).
- `detent-agent --status` prints `durable revision=N knob_count=M` instead of
  `OK version=N knob_count=M`, and prints it whenever a generation directory is
  set.
- Roadmap item 4 (a path for unmanaged agents and clients to receive authority
  data) is dropped.
- The systemd `detentd` unit and the Homebrew caveats and test no longer call
  `detentd` a distributor.

### Legacy removal (wave 2, step F)

- Removed the `detent_http` library (`detent_http::detent_http`,
  `make_http_poll_source()`, the blocking HTTP client) and its two tests. No
  Detent daemon serves `GET /v1/snapshot`, so nothing polled it.
- `detent-agent` and `detent get` no longer take `--source-address`,
  `--source-http`, `DETENT_SOURCE_ADDRESS`, `DETENT_SOURCE_HTTP` or the
  `source_address` and `source_http` config fields; each is now an unknown
  argument or field. With no defaults and no source, the agent no longer
  polls `127.0.0.1:8317`.
- `InitOptions::source_poll` stays as the custom-poller seam.

### Legacy removal (wave 1)

- Removed `tests/e2e`, the classic distributor process harness.

### Authority: retained-state envelope and replacement-pool funding

- New optional `retained_state` object in `authority.json`: `command_bytes`,
  `log_bytes`, `log_entries`, `batch_bytes` and `batch_entries`. It bounds the
  history a node recovers, receives and checkpoints, apart from the operating
  limits. Omitted fields default to the operating defaults (1 MiB, 64 MiB,
  65,536 entries, 2 MiB, 256 entries), so a configuration without the object
  recovers exactly what it did. Each field is between 1 and the library
  ceiling and at least the operating limit it covers. The values are local:
  they are not part of the replicated profile, the journal, checkpoints or the
  peer fingerprint.
- Lowering `consensus_limits` no longer lowers the envelope. A node that lowers
  its operating log bounds still recovers history written under the larger ones.
- Startup funding now counts the retained suffix. The replacement pool
  (`journal_limits.replacement_bytes`, 512 MiB by default) must hold the
  application image, the encoded retained suffix and 4096 bytes. The default
  needs 336,931,316 bytes. A node that falls short refuses to start with
  `authority runtime cannot fund configured limits: the replacement pool holds
  N bytes but the application image (A) + the encoded retained suffix (S) +
  the publication allowance (4096) need R, short by D bytes; raise
  journal_limits.replacement_bytes, or lower application_limits.image_bytes or
  retained_state.log_bytes and log_entries`.

Upgrade note. Three kinds of configuration are refused that were accepted
before, and neither changes any data:

1. A configuration that lowered `consensus_limits` and sized
   `journal_limits.replacement_bytes` for the lowered log. The requirement now
   uses the default envelope (64 MiB, 65,536 entries). Raise
   `journal_limits.replacement_bytes` by the shortfall the error names, or set
   `retained_state.log_bytes` and `log_entries` to the lowered values once the
   store holds no larger history (recovery outside an explicit envelope fails
   without truncating the store).
2. A configuration with a `retained_state` field below its operating limit,
   zero, or above the library ceiling. Remove the field or move it into range.
3. Code that builds `NodeOptions` with operating `consensus` limits above the
   defaults: set `scheduling.retained` to at least the same values.

A configuration with the default `consensus_limits` and a replacement pool that
funded the previous check is accepted unchanged: the requirement is the same.

### M1: dependency pins at the libraries' heads (cmake/dependencies.json)

Every Kronuz pin moved to its library's main head, following "Updating an application (for agents)" in
cpp-libraries' `docs/UPGRADING-2026-10.md`, dependency first. No pin falls inside a range of its
"Commits not to pin". Old is `Detent:bf20f94`, the last main commit before the first pin moved
(`Detent:85253b5`, 2026-11-02); new is the manifest of `Detent:f7267d5` (2026-11-04). The input-class
diffs, the hot-path A/B and the rerun commands are in
[docs/perf/m1-w5-evidence.md](docs/perf/m1-w5-evidence.md).

| Library | Old pin | New pin | Note |
|---|---|---|---|
| atomic-shared-ptr | `b7bf5ef` | `61299bd` |  |
| char-classify | `306ecd9` | `25decb5` |  |
| cluster | `f053a82` | `e4fc57d` | dropped by legacy wave 2 (was e4fc57d at f7267d5) |
| completion | `5251cf9` | `9acfba3` |  |
| compressors | `ed590c8` | `5566260` |  |
| consensus | `b7073bb` | `cef08eb` |  |
| constexpr-phf | `1a03523` | `4b669b6` |  |
| endian | `f6269f0` | `6b223e2` |  |
| enum-reflection | `9b5ecea` | `a7202ad` |  |
| flat-map | `a9a52b0` | `06fc29e` |  |
| generation-cache | `01af1b2` | `3d45c49` | dropped by legacy wave 2 (was 3d45c49 at f7267d5) |
| hashes | `7ab0c23` | `03ab2d6` |  |
| http | `6a07f99` | `c813531` |  |
| http-log | `2f066ee` | `4e4b79f` | dropped by legacy wave 2 (was 4e4b79f at f7267d5) |
| http-parser | `178f873` | `fcdad32` |  |
| journal | `57e6250` | `bfad97f` |  |
| logger | `8c5afcd` | `f61d8df` |  |
| lru-cache | `7dd23eb` | `-` | removed |
| metrics | `c975765` | `9db73de` |  |
| radix-router | `d035744` | `e67543d` |  |
| random | `6487253` | `f8b9348` |  |
| reactor | `fe8c5ce` | `ab69cf2` |  |
| repr | `0fe9a1e` | `d2eb895` |  |
| scheduler | `fb464cb` | `d57e8fc` |  |
| sha256 | `2bb34f0` | `f24692f` |  |
| split | `aa9857a` | `d48a28e` |  |
| stash | `fa1936e` | `b7d8876` |  |
| static-string | `eb1efcb` | `7eaa8f9` |  |
| strict-stox | `21b7941` | `b846098` |  |
| strings | `b42d3c4` | `6799a6e` |  |
| task | `-` | `bdafa5f` | added |
| term-color | `534e309` | `76a400b` |  |
| threadpool | `628679a` | `63e03a5` |  |
| varint | `21af82a` | `1c3e526` |  |

`task` is new (the one coroutine type). `lru-cache` left with the verification cache limit. `cluster`,
`generation-cache` and `http-log` left with the classic distributor (legacy wave 2); their rows show the
pin they had at `Detent:f7267d5`. `tools/check_pins.py` (CI job `pin-check`) fails when a shared
dependency's pin differs from the pin of a library that declares it.

Sections of the guide acted on:

- **http**, **reactor**, **http-log** (code change): handlers, routes and `BodyStream` are `task::task`;
  `HttpAsioService(handler, reactors, in_flight_limit, factory, maximum_connections)`; `reactor::TcpServer`
  takes `(reactors, admission_limit, session)` and there is no worker pool; `AccessLog::handle` is a
  coroutine. Detent's handlers, owners and its own asio coroutine code moved to `task::task` (W2); CI
  fails if an asio coroutine construct returns.
- **scheduler** (code change): no task calls `shared_from_this()` on a scheduler task.
- **completion**, **journal**, **consensus**: completion's read-only opens are pure virtuals of `IO`; the
  `IO` subclasses in journal's test helpers and in Detent's tests implement them.
- **hashes** and **crc32c**: Detent includes no `crc32c.hh`; journal fetches `crc32c`.
- **http-parser**, **compressors**, **strict-stox**, **enum-reflection**, **random**, **logger**: read for
  their "Detect" lines; Detent's inputs for each were diffed through the old and the new build (below).
- **allocators**, **metrics**, **generation-cache**, **btree**: checked against "Commits not to pin" only.

Results that changed (old build against new, same inputs):

- **http-parser strictness** (`http-parser:21c8f30`, `ab8377c`, `62ac7f3`, `0aeec14`, `e2f23b9`,
  `5cb4409`). Detent does not set `lenient_http_headers`, so a request with a bare LF, an obs-fold line,
  a `Transfer-Encoding` other than exactly `chunked`, a missing or odd HTTP version, or a
  `Content-Length` in a trailer is now rejected: `detentd` closes the connection without a response
  where it answered before (200, or 400 for the three that reached the body parser). In lenient mode the
  parser reports what it reported before. Requests that were well formed are answered byte for byte as
  before.
- **compressors** (`compressors:f157dab`, `8f57675`, `533bee4`): a damaged LZ4 stream throws
  `LZ4CorruptVolume` (a subclass of `LZ4Exception`) where it threw `LZ4Exception`; one crafted LZ4 record
  that aborted the process now throws; a zstd frame that claims 1 TiB throws `ZstdCorruptVolume` where
  it hung or threw `bad_alloc`; a zlib preset dictionary throws `DeflateException`. Valid streams:
  compressed bytes, decoded bytes and XXH32 digests are identical.
- **strict-stox** (`strict-stox:7d7f5dd`, `a1b03fb`): a `--just-knob` integer of 64 bytes or more
  (`000...007`) is accepted where it was refused. Every shorter input gives the same value, `errno` and
  `idx`.
- **enum-reflection** (`enum-reflection:7d394c5`, `2a5da3a`): a string forged to share a member's 64-bit
  hash is a miss. Every member name, case variant and near miss answers as before.
- **random**: the sequence differs (a new generator; there is no seed API). Ranges stay inclusive and in
  range, and draws stay uniform.
- **logger**: lines logged after `Logging::finish()` are written at the call (`logger:a48bce8`, which the
  old pin of 2026-10-17 predates) where they were lost. Every other byte of the scripted run is identical.
- **http** (http's CONNECT handling): the 404 answer to `CONNECT` carries `Connection: close`.
- Unchanged: the snapshot JSON (parse, merge, serialize, diff, patch), the distribution wire frames
  (raw and zstd), the local images, and the persisted `snapshot.json`. At 5M knobs the prepared reader
  metadata differs only in the generation id and the digests that cover it; the root image and the 1,441
  packs are reused.

Owner decisions taken:

- **One coroutine type** (November 2, `docs/SHARED_LIBRARY_DECISIONS.md`, W2a): http handlers, routes and
  `BodyStream` are `task::task`; no mixed shape (http's connection loop moves to task too).
- **Pushes to library mains**: the library commits in `docs/status/m1-pins.md` (completion, hashes, reactor,
  repr, sha256, term-color, enum-reflection, strings, cluster, http-parser, http, journal, consensus), each
  CI-green before the next; the owner authorized the scheduler, logger and http-log pushes explicitly.
- **Post-W2 ports**: `detentd --port 0` and the relay's `"port": 0` bind a free port and log it
  (`docs/mechanisms/bound-port-report.md`); system fixtures start on port 0.

Open for the owner: the http-parser strictness above changes what clients of `detentd` see for
malformed requests (connection closed, no response). Setting `lenient_http_headers` restores the old
answers.

### Behavior changes (W2)

- `detentd` has no worker pool: `--workers`, `DETENTD_WORKERS` and the `workers` config field
  are removed, and naming one fails with an error that says so.
- A PUT or PATCH over 16 MiB (and an authority body over `request_bytes`) is answered 413
  instead of dropping the connection.
- Classic PUT and PATCH commit on one owner thread and publish an immutable snapshot, so GET
  and health never wait on a commit; a write refused for lack of a write slot gets 503 with
  `Retry-After: 1` before any body byte.
- The authority answers a command, read or result through an owned stream that keeps the
  connection alive, and a reply over the reply budget is a JSON 503 (`outcome_unknown` for a
  command, `admission_full` for a read) instead of a closed connection.
- Authority `GET /v1/health` with a body is answered 400 `health_body_forbidden` instead of a
  dropped connection.
- Shutdown answers waiting writers by the outcome contract at once (`begin_shutdown()`).

`src/CMakeLists.txt` no longer declares http-parser at `GIT_TAG main`; the
manifest pin (`c27615d`) is the only declaration, so every Kronuz dependency
is pinned at a commit.

### Fixes

- `Client::~Client()` joins the updater thread before `on_update_` and the
  snapshot state it reads are destroyed. ThreadSanitizer reported the race
  in `test_file_source`.

### Tests

- `tests/support/allocation_probe.hh` is the one way a test observes heap
  allocations. It defines the replaceable global allocation functions around
  a per-test probe, which may throw to inject a failure. Under
  ThreadSanitizer on Linux the runtime owns those functions, so the header
  calls the probe from the sanitizer's malloc hook instead, and the three
  tests that inject failures (`test_model_alloc_failure`,
  `test_client_publication_failure`, `test_local_directory_ownership`) are
  not built there.
- `test_local_paged_collection_crash` runs with `die_after_fork=0` under
  ThreadSanitizer: it forks a process that has run threads.
- The `linux-tsan` CI job runs.
