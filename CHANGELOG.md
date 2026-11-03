# Changelog

## Unreleased

### Legacy removal (wave 1)

- Removed `tests/e2e`, the classic distributor process harness.

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

Not moved: http (`6a07f99`; head `73cfa02`), reactor (`fe8c5ce`; head
`2fd30c8`) and http-log (`2f066ee`; head `616c80a`). Their heads replace the
`HttpHandler` virtuals (`handle`, `should_offload`, `on_request_body`,
`on_error`) with `task::task` coroutine handlers and change the `Reactor`
and `TcpServer` constructors. That rewrite of `detentd`, the authority and
relay services and `test_transport_https` is workstream W2 (S2 to S5), so
the three pins move with it.

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
