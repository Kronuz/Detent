# Changelog

## Unreleased

### Legacy removal (wave 2)

- Removed the `detent_publish` example.

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

### Dependency pins (cmake/dependencies.json)

Moved to each library's main head, following the "Updating an application"
steps of cpp-libraries' `docs/UPGRADING-2026-10.md`. No pin falls inside a
range of its "Commits not to pin".

| Library | Old | New |
|---|---|---|
| atomic-shared-ptr | `b7bf5ef` | `9af0aa7` |
| char-classify | `306ecd9` | `57613ed` |
| cluster | `f053a82` | `428d57d` |
| compressors | `ed590c8` | `5c54128` |
| constexpr-phf | `1a03523` | `1d82461` |
| enum-reflection | `9b5ecea` | `653fb34` |
| endian | `f6269f0` | `a24942e` |
| flat-map | `a9a52b0` | `9fe0bd9` |
| generation-cache | `01af1b2` | `38a2d39` |
| hashes | `7ab0c23` | `e3bf5b6` |
| http-parser | `178f873` | `c27615d` |
| logger | `8c5afcd` | `d63fec3` |
| lru-cache | `7dd23eb` | `df0ebc8` |
| metrics | `c975765` | `db490b4` |
| radix-router | `d035744` | `74ab7ce` |
| random | `6487253` | `e040c2b` |
| repr | `0fe9a1e` | `87450b4` |
| scheduler | `fb464cb` | `1e5ccc9` |
| sha256 | `2bb34f0` | `f4a3463` |
| split | `aa9857a` | `40af1a2` |
| stash | `fa1936e` | `d2e8e59` |
| static-string | `eb1efcb` | `46561fa` |
| strict-stox | `21b7941` | `1f9190c` |
| strings | `b42d3c4` | `1be3848` |
| task | `6558c2e` | `d080786` |
| term-color | `534e309` | `94df6e5` |
| threadpool | `628679a` | `3f1fbcf` |
| varint | `21af82a` | `ac6003e` |

completion (`576618d`), journal (`d6d009e`) and consensus (`666cdfc`) are at
their heads already.

Moved with the W2 rewrite (S2 to S5): http (`6a07f99` to `b18e115`), reactor
(`fe8c5ce` to `950a375`) and http-log (`2f066ee` to `3b49b60`). Their heads replace the
`HttpHandler` virtuals with `task::task` coroutine handlers, remove the worker pool and
change the `Reactor`, `TcpServer` and `HttpAsioService` constructors. The sections acted on:
http (handlers on `task::task`, `HttpAsioService(handler, reactors, in_flight_limit,
factory, maximum_connections)`, one `ConnectionFactory`, the body cap through
`set_max_body`, the four teardown phases), reactor (no worker pool) and http-log
(`AccessLog::handle` on `task`).

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
