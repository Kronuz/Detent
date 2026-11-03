# Legacy removal inventory

Base: `Detent:74e07c9` (github/main). Owner ruling: no legacy modes; greenfield. Read-only inventory plus a cut plan; nothing is deleted here. Nothing was built or run: every claim is from reading source (file:line) and `git grep`. Line counts are `wc -l` of the named files. W2 is `github/phase/w2-s2` (50 files, +1574/-1001).

## Summary

| # | Item | Verdict | Lines (counted) |
|---|---|---|---|
| 1 | Classic distributor (`detentd` without `--authority`) | DELETE | src 1235 + tests 2118 + e2e 574 + bench 260 |
| 2 | Classic Raft over `cluster::Bus` | DELETE (drops `cluster` and `generation_cache` pins) | src 169 + CLUSTERING.md 151 + tests 593 (inside item 1 totals) |
| 3 | JSON polling client `detent_http` | DELETE (move `address.hh`) | 1053 incl. tests |
| 4 | Legacy agent profile (shm seqlock, unmanaged `detent-agent`, `detent get` shm path) | DELETE | 1302 + 67 + ~250 inside `agent/main.cc` |
| 5 | Image format flat-v1 | DELETE (no measured reason) | 504 flat-only + branches in ~6 files + fixtures |
| 6 | Legacy import/export tool, `json_patch`, classic-only JSON snapshot code | DELETE import/export and `json_patch`; KEEP knob-definition schema; ASK on baked defaults | 292 + 546 |
| 7 | Bindings | ASK (they use only the file source; they must be rebuilt on the reader) | 0 removed, rewritten |
| 8 | Examples, tests, benchmarks, CI, docs, website | DELETE with their owners | see below |
| 9 | Other legacy words | mixed, see item 9 | |
| 10 | `detent::Client` updater, file and poll sources | DELETE the updater; KEEP the reader pieces (see Client section) | |

Counted total removed: about 8,100 lines of C++, scripts and CMake (sum of the rows above, overlaps removed), plus roughly 2,000 lines of docs and website pages that are deleted or rewritten (estimate, 100 files mention a mode; list at the end). The client rewrite and flat-branch removal add more than are counted here.

## Cross-cutting facts that shape the plan

1. **`detentd` is one binary for both modes.** `src/detentd/main.cc` (335 lines) selects classic or authority at `main.cc:250-268` (`--authority` present or not; `Distributor` at `:268`). The authority is compiled only under `DETENT_BUILD_DISTRIBUTOR AND DETENT_BUILD_AUTHORITY` (`src/CMakeLists.txt:178-181`, `src/detent_authority/CMakeLists.txt:9,15,22,28`, `CMakeLists.txt:88-94`). So removing the classic mode means renaming or deleting `DETENT_BUILD_DISTRIBUTOR` (default ON, `CMakeLists.txt:41`), not only deleting a directory. Authority code does not include `detentd.hh`; the only link is the shared executable.
2. **Nothing outside `tests/detentd`, `benchmarks/{detentd_fanout_bench.cc,container-alternative/publication_bench.cc}` and `tests/detent_agent/test_agent_http.sh` links `detentd_lib` or runs the classic mode** (CMake: `grep detentd_lib` over every CMakeLists). `tests/detent_system` and `tests/detent_authority` use `detentd --authority` only.
3. **W2 is investing in the classic mode.** `phase/w2-s2` changes `src/detentd/detentd.cc` (556 lines), `detentd.hh` (209), adds `tests/detentd/test_distributor_owner.cc` (+228), `docs/mechanisms/classic-publish.md`, and records an open decision about Raft apply-retry that only exists because of the classic Raft (`docs/status/http-coroutines.md`, "Blocked on"). All of that is dead work if the classic mode is deleted.

## 1. Classic distributor

What: `detentd` without `--authority`: `Distributor` HTTP handler (`GET/PUT/PATCH /v1/snapshot` with ETag/304, composed-diff cache, `GET /v1/health`), persistence to `snapshot.json` (`main.cc:176-182`), config `detentd.json` fields `persist_path`, `reactors`, `workers`, positional `<port> <persist_path>` (`main.cc:139-143`), `DETENTD_PERSIST_PATH`, build option `DETENT_BUILD_DISTRIBUTOR`.

Files:
1. `src/detentd/detentd.cc` 613, `detentd.hh` 305, `raft_delegate.cc` 70, `raft_delegate.hh` 96, `CLUSTERING.md` 151 (1235 total). `main.cc` 335 stays but loses roughly 60% (usage text, `--cluster`, `--raft-timing`, `--persist-path`, positional syntax, `Distributor` branch, access log).
2. Tests (`tests/detentd/`): `test_diff_cache`, `test_distributor_basic`, `test_distributor_client_integration`, `test_http_connection_aging`, `test_load_fanout`, `test_multi_node_poll_source`, `test_publication_atomic`, `test_raft_application_retry`, `test_raft_cluster`, `test_cli_defaults.sh`, `CMakeLists.txt` (2118 total). `test_cli_defaults.sh` also checks `detentd` default CLI behaviour, so port its authority-relevant assertions before deleting. `tests/detent_agent/test_agent_http.sh` (67) runs the agent against classic `detentd`.
3. `tests/e2e/detent_e2e.py` 499 + `README.md` 75: the classic Raft process harness. Not referenced by CMake or CI (grep: no match in `.github` or any CMakeLists), so it is already orphaned.
4. Benchmarks: `benchmarks/detentd_fanout_bench.cc`, `benchmarks/container-alternative/publication_bench.cc` (260), CMake guarded at `benchmarks/CMakeLists.txt:29-32`. Recorded JSON results (`distributor-publication-preliminary-20261020.json` and others) are history, not code.
5. Packaging `detentd` units stay (they will run the authority); `packaging/*/detentd.*` need a text pass only.

Reverse dependencies: `detentd_lib` is linked by 9 test targets and 2 benchmarks (above) only. `detent_http` and `cluster` are its inputs (items 2, 3). `generation_cache` is used only by `detentd.hh:43,263` and `detentd.cc:489` plus `tests/detentd/test_diff_cache.cc`.

Used by managed, authority, relay, agent: no. The authority does not include `detentd.hh`; the managed agent and relay never touch `/v1/snapshot` (they use `/v2/*`). Exception to handle: `detentd --status` for a plain-HTTP authority probes `/v1/health` through `detent_http::HttpConnection` (`main.cc:211-216`); the authority needs its own health probe (the TLS path already uses `detent::transport::https_health`, `main.cc:203-210`).

Verdict: DELETE.

## 2. Classic Raft (`--cluster`, `cluster::Bus`)

What: `DetentRaftDelegate` over `cluster::Bus` UDP multicast plus `cluster::Raft` (`raft_delegate.*`, `detentd.cc` cluster paths, `--cluster`, `--raft-timing`, `Options::Cluster`).

Does removal drop the `cluster` library? Yes for Detent's own code. Every `#include <raft.h>`, `<bus.h>` and `cluster::` use is in `src/detentd/` and `tests/detentd/test_raft_*.cc` (git grep, excluding docs). The authority uses `consensus::consensus` and `consensus::transport` (`src/detent_authority/CMakeLists.txt:10,24`), `completion::completion` comes from its own repo (`cmake/dependencies.json`, name `completion`, `Kronuz/completion`), and `http::http` from `Kronuz/http`. So the cut is:
1. `FetchContent_MakeAvailable(http_lib cluster generation_cache)` at `CMakeLists.txt:89` becomes `http_lib` only; delete the `cluster` and `generation_cache` entries from `cmake/dependencies.json` and `DependencyPins.cmake`, and `cluster::cluster generation_cache::generation_cache` from `src/CMakeLists.txt:118-119`.
2. Not verified: whether the `consensus` repo's own CMake fetches `cluster` internally. Check the consensus CMakeLists before declaring the pin gone (`docs/status/http-coroutines.md` mentions `cluster` source "as reviewed cluster sources" for completion historically; completion now has its own repo).
3. `-DDETENT_BUILD_AUTHORITY` text "reviewed cluster sources" (`CMakeLists.txt:44`, `main.cc` error strings) needs rewording.

Verdict: DELETE. Removing it also closes W2's open "Raft apply retry" decision.

## 3. JSON polling client (`detent_http`)

What: `src/detent_http/{http_client.cc 452, http_source.cc 126}`, `include/detent_http/{http_client.hh 127, http_source.hh 62, address.hh 6}`, `tests/detent_http/*` (2 tests), CMake target `detent_http` / `detent_http::detent_http` (`src/CMakeLists.txt:80-90`). `make_http_poll_source` plugs into `InitOptions::source_poll`.

Reverse dependencies (grep of every CMake and include):
1. `detentd_lib` (`src/CMakeLists.txt:116`), `detentd/main.cc:25-26,211-215`.
2. `detent-agent` (`src/CMakeLists.txt:146`; `agent/main.cc:9,18,128,137`).
3. `benchmarks/detentd_fanout_bench`, `tests/detentd/*`, `tests/detent_http/*`.
4. **`include/detent_http/address.hh` (6 lines, `kLoopbackHost`) is included by the authority** (`src/detent_authority/service.hh:11,35`) and by `agent/main.cc:18` and `detentd/main.cc:26`. Move the constant into `detent_cli` (which already owns `kDefaultPort`) first.

Used by managed, authority, relay: no (the managed path uses `detent_delivery` TLS clients and `detent_transport/https_client.hh`). Also check `compressors` stays: it is still fetched at top level and used by distribution codecs.

Verdict: DELETE, after moving `address.hh`.

## 4. Legacy agent profile

What:
1. Shared-memory seqlock image: `src/detent_agent/shm_format.hh` 137, `shm_writer.cc/.hh` 230, `shm_reader.cc/.hh` 340, `agent.cc/.hh` 116 (the unmanaged `Agent`: one `detent::Client` republishing into shm via `InitOptions::on_update`). Format limits: 4 MiB image and 32-bit offsets (`shm_format.hh`, README ~412-428 "IPC design").
2. Unmanaged `detent-agent`: `run_agent` non-managed branch (`agent/main.cc:205-240`), `build_client_options` (`:110-150`), `run_foreground` (`:162-203`), `--source-path`, `--source-address`, `--source-http` alias (`:550`), `DETENT_SOURCE_*` env (`:562`), `--defaults`/`--poll-interval-ms` for that branch, `cmd_status` shm branch (`:242-275`).
3. `detent get` legacy path: `cmd_get` after the managed branch (`agent/main.cc:437-520`), `launch_agent` (`:330-362`), which `cmd_managed_get` also calls at `:403` when `g_managed_config` is set (keep the managed auto-start, drop the shm one at `:452`).
4. Tests: `test_agent_basic`, `test_shm_concurrency_stress`, `test_snapshot_canonical`, `test_agent_http.sh`, parts of `test_agent_cli.cmake` and `test_system_agent.sh`; `benchmarks/detent_agent_shm_bench.cc` and `detent_scale_bench.cc` (links `detent_agent_lib`). Total counted 1302 + 67; `agent/main.cc` loses about 250 lines (estimate).

Reverse dependencies: `detent_agent_lib` is linked by `detent-agent`, `detent-reader` (`src/CMakeLists.txt:134-139,155`), `test_agent_*`, two benchmarks. `agent/config.cc`, `reader_capacity.cc` and `managed_host.cc` are managed code and stay, but `detent_agent_lib` needs splitting: the managed host does not use `Agent` or shm (it uses `detent::delivery_config`).

Used by managed: no. `README` call-out "README ~434-448" is the Language bindings section and the shm paragraph at ~412-428.

Verdict: DELETE. Open question: Q3 (non-C++ readers) below, answered by the new reader libraries.

## 5. Image formats: flat-v1 vs paged-v2

Evidence that flat-v1 is legacy and not a small-dataset format:
1. Default is PagedV2 in code: `include/detent_delivery/coordinator.hh:30`; `configuration.cc:140-146` only overrides when `image_format` is present. The website says the opposite (`website/.../reference/configuration.md:103` "flat-v1 (default)"): the doc is stale, not the code.
2. `docs/PRODUCTION_CONTRACT.md:25-27`: paged is the default; flat and `reader_validation: "full"` "support compatibility deployments". `docs/PREPARED_ACTIVATION_ARCHITECTURE.md:9` says explicit flat is "for compatibility".
3. No benchmark in `benchmarks/` or `docs/measurements/` shows flat winning at any size: `paged-snapshot-measurements.json` has flat rows only as the comparison baseline (`"layout": "flat"` at lines 5, 43); I found no tiny-dataset result. I did not run anything, so "no measured reason" means none recorded.
4. Flat is the cost of a second recovery path: `generation.cc:129-185` (`recover_routed` picks `legacy` vs `paged` context by `layout_version`), `disk_store.cc:399-401,1701-1714` (`StoreOpenMode::UpgradeLegacy`, `legacy_recovery`), `paged_named_root.cc` (`legacy_anchor`, lines 46-57,115,280,300-337), `coordinator.cc:93,180,1110`, `managed_host.cc:144`.

Flat-only files (counted): `src/detent_local/builder.cc` 335, `include/detent_local/builder.hh` 32, `candidate_flat_validator.cc/.hh` 79, `flat_state_rows.hh` 39, `validated_flat_state.hh` 19 = 504. Shared and trimmed, not deleted: `layout.cc/.hh` (header constants are used by 25 paged files), `view.cc`, `generation.cc`, `disk_store.cc` (1806 lines), `borrowed_snapshot_source.cc` (used by the paged builder, `paged_snapshot_builder.cc:2,41-47`), `ownership.cc` (`layout_version`), `snapshot_source.cc`.

Dependents: 26 test files and `benchmarks/local_image_bench.cc` use `ImageBuilder` or `detent_local/builder.hh` (fixtures such as `tests/detent_local/snapshot_fixture.hh`); they need paged fixtures first. Also delete `StoreOpenMode::UpgradeLegacy` and the `--upgrade-managed` agent flag (`agent/main.cc:221`, `include/detent_local/disk_store.hh:8`), config key `image_format`, and `LocalImageFormat`.

Verdict: DELETE. Sequencing: this is the largest and riskiest cut; do it after the fixtures move. ASK 3 covers `reader_validation: "full"`.

## 6. Legacy import tool, JSON snapshot schema, `json_patch`

1. `detent-authority-admin import|export` (`src/detent_authority/admin.cc` 159, `admin_runtime.cc/.hh` 107, `tests/detent_authority/test_migration.sh` 26): import is classic JSON into a fresh authority; export writes JSON back "LEGACY_VERSION" (`admin.cc:63-64,139`). `admin_runtime` is used only by `admin.cc`. The tool has no other command. Website: `reference/cli/detent-authority-admin.md`, `operations/upgrading.md`, `guides/durable-authority.md`, `operations/authority.md`, `installation.md`, `reference/cli.md`, `lib/sidebar.mjs`. Verdict: DELETE (greenfield: nothing to migrate from).
2. `json_patch.hh/.cc` (81 + 212) and `SnapshotPatch` (`model.hh`): used by `Client::try_apply_source_patch` (`client.cc`), classic `detentd`, tests (`test_json_patch`, `test_duplicate_precedence`, `test_json_strings`, `test_model_alloc_failure`) and benchmarks (`detent_scale_bench`, `detent_authority_bench`, `lifecycle_bench`). Managed code does not use it (`src/detent_authority`, `detent_delivery`, `detent_relay`, `detent_local` have no match). Verdict: DELETE.
3. JSON snapshot schema (`schema_version`, `knobs`, `parse_source_snapshot`, `serialize_snapshot`, `merge_over_defaults`): used only by the classic server, `Client` sources, `admin.cc`, `examples/publish_snapshot.cc`. DELETE those entry points.
4. **KEEP the knob-definition schema** `{"type":"int","default":42}` / bool with `overrides` and `rollout_pct`: `parse_knob_def`, `write_knob_def`, `nesting_depth_exceeds` (`json_schema_internal.hh`) are used by the authority (`service.cc:4`), managed delivery config (`detent_delivery/configuration.cc:4`), relay config (`detent_relay/configuration.cc:2`), `detent_distribution_http/session_request.cc:2`, `detent_cli/config.cc:3`. Keep them as the definition parser, renamed and moved to a `definition` header (the "internal" naming is wrong once it is the only JSON schema).
5. `parse_defaults` and `Snapshot` as the baked-defaults carrier: see ASK 2.

## 7. Python and Rust bindings

Both wrap `detent::Client` with `InitOptions{defaults_path, source_path, cli_overrides, poll_interval}` only (`bindings/python/detent_python.cc:48-52`, `bindings/rust/src/shim.cc:25-35`; the comments at `detent_python.cc:10-16` and `lib.rs:9-11` say HTTP is not wired). So their only live path is the JSON file source with its updater thread (`run_file_source`). They never use `read_source`, the managed generation, or the agent. Tests: `bindings/python/tests/test_detent.py`, `bindings/rust/tests/integration.rs`.

Verdict: ASK is not needed for the direction (they must move to the reader); the rewrite is covered by the reader proposal below. The file source cannot be kept, since it is the JSON polling path.

## 8. Examples, tests, benchmarks, CI

1. `examples/publish_snapshot.cc` (`detent_publish`, 160): atomic JSON file publisher for the file source. DELETE. `examples/demo.cc` (44) uses baked defaults only; keep or drop with ASK 2. Fix the comment at `examples/CMakeLists.txt:3-6`.
2. `tests/e2e/*`: DELETE (574, orphaned).
3. `tests/cmake_consumer/main.cc` mentions a mode term once: check it builds against the reduced API.
4. CI: `.github/workflows/ci.yml` has no job for these modes, but the default build (`DETENT_BUILD_DISTRIBUTOR=ON`) runs all classic tests; W2 adds a `source-checks` job (T10, T12) that greps `--workers`. No CI edit is needed beyond what the CMake change implies.
5. Benchmarks: see items 1, 4, 6. `detent_update_bench`, `detent_scale_bench`, `detent_read_bench` (explicitly "no background thread"), model benchmarks use `Snapshot`/`parse_*`; they need a decision with ASK 2.

## 9. Everything else marked legacy, classic, compat, deprecated

Remaining `legacy` hits in `src include bindings tests examples packaging`:
1. `StoreOpenMode::UpgradeLegacy`, `legacy_recovery`, `legacy_anchor`, `RecoveryContext legacy` (flat; item 5): DELETE.
2. `src/detent_authority/service.cc:1204-1400,1741,2057-2184,2720` `legacy_deliveries`, `legacy_ready`: the name only; it is the queue for control deliveries on the managed authority. KEEP; rename (not legacy). W2 rewrites this file (442 lines), so rename later.
3. `service.cc:1015` "Compatibility for synchronous recording/embedded writers": a sync path; ASK-free, investigate when W2 lands (probably test-only; do not decide here).
4. `tests/detent_authority/test_distribution_ownership.cc:292,500-525` "legacy error header" and "legacy_body" on `/v2/distribution`: a v2 route test name; confirm it is not an old protocol (`/v2/distribution` is the live managed route). KEEP, rename.
5. `agent/agent.hh:12` "no wire-protocol compatibility kept" (comment; goes with agent.hh); `include/detent/global.hh:4` "source (pyjk-style) compatibility" (see Client section: ASK 4); `src/detent_delivery/...test_source_selection.cc:21` variable named `legacy` for a non-pooled selection (rename only).
6. `src/detent/client.cc:113`, `client.hh:118` "legacy file/poll sources": goes with the updater.
7. `docs/` and `website/`: `legacy`/`classic` in README, ACTORS.md, DEPENDENCY_UPGRADES.md, LIBRARY_BOUNDARIES.md and many history docs (list below).
8. `README.md:21` says outright: "the code calls it legacy, and it is still built by default."
9. `v1/` routes: `/v1/snapshot`, `/v1/health` classic only. The authority has its own health route (`website/.../http-api/health.md` should be re-read); `GET /v1/health` stays only if the authority serves it (`main.cc:211` probes it for a non-TLS authority).
10. Deprecated: only rapidjson `std::iterator` suppressions (`src/CMakeLists.txt:17`, `bindings/rust/build.rs:39`): third-party, KEEP.

## Client (`detent::Client`, `include/detent/client.hh` 254, `src/detent/client.cc` 466)

Pieces:
1. Updater thread `updater_thread_` (`client.hh`, `client.cc:128-139`), `run_file_source` (`client.cc:293`), `run_generic_source` (`:351`), `try_apply_source_bytes`, `try_apply_source_patch`, `sleep_with_stop_check`, backoff, `refresh_alert_after`, `Metrics` fields `snapshot_swaps/refresh_failures/stale_updates/source_connected`: DELETE (they exist only for JSON sources).
2. `InitOptions::source_path`, `source_poll`, `source_id`, `poll_interval`, `on_update`, `SourcePayload`, `SourcePoll`, `live_snapshot_`, `merge_over_defaults`: DELETE.
3. `max_knobs`: only bounds the JSON parse (`client.cc:73-84`): DELETE with the parser; the image has its own limits (`ViewLimits`).
4. `defaults_path` / baked defaults merged under live data: ASK 2.
5. `cli_overrides` / `OverrideTable` (`overrides.hh/.cc` 89 lines): KEEP if ASK 2 keeps overlays; it is evaluated before the source in `FrozenEvaluation` (`client_source.cc:41-46`).
6. `read_source` / `ReadSourceFactory` / `ClientReadView` / `EvaluationView` (`read_view.hh`): KEEP as the seed of the new reader, but see the shape below (virtual dispatch, shared_ptr copies and std::function factory are not thin).
7. `Client(Snapshot)` test ctor and `detent::testing::TestKnobs` (`testing.hh/.cc` 90), `detent::global` (`global.hh/.cc` 122, "pyjk-style" free functions): KEEP `testing` as a test double; global: ASK 4.
8. `Metrics` read counters (`reads_ok`, `reads_missing`, ...): ASK-free, KEEP as optional (relaxed atomics), but see per-read cost.

### Which path `detent get` uses today

`src/detent_agent/main.cc` built as `detent` (`DETENT_READER`, `src/CMakeLists.txt:155-160`):
1. `cmd_get` (`agent/main.cc:437`) branches on `g_generation_dir`; managed reads go to `cmd_managed_get` (`:364`). Non-managed goes to the shm reader (`:440-520`, legacy).
2. `cmd_managed_get` (`:364-435`): builds `ReaderCapacity` limits, `detent::InitOptions` with `defaults_path`, `detent::local::ClientSourceOptions`, sets `options.read_source` to a lambda constructing `LocalReadSource` (`:379`), then `std::make_unique<detent::Client>(options)` (`:391`), `source->refresh()` if not prepared, `client->read_view()` and `try_read`.
3. `try_read` (`:316`, `template <class Reader>`) evaluates as bool first (`reader.eval`), and on `WrongType` calls `reader.get`: **two lookups for an int knob**, and each failing bool lookup builds a `KnobError` with a `std::string` name and detail (allocation).
4. `LocalReadSource` ctor (`client_source.cc:107-131`) with `prepared_publisher` set calls `refresh()` synchronously (`:112`), which goes through `GenerationReader::refresh` to `PreparedReader::open` (`src/detent_local/prepared_reader.cc:89-130`: `open_publisher_directory`, `fstat`, `openat` selected record, lease `openat`, `fstat`, `fstatat`, flock) and `local::detail::LazyPageAccess` (`lazy_page_access.cc`).
5. `local_view` = `detent_local_view` (`src/detent_local/CMakeLists.txt:1-6`), `local_client` = `client_source.cc` (`:7-9`).
6. Evaluation over the mapped view: `ReadView::try_eval` (`src/detent_local/view.cc:373-413`) reads `BorrowedDefinition` straight from mapped pages (`borrowed_definition.cc`); no `Snapshot` is built for the live data. A `Snapshot` exists only as the baked-defaults fallback (`FrozenEvaluation::fallback`, `client_source.cc:21-23,48-53`) and as the factory's parameter type.

### Evaluation logic, in three places (one too many)

1. `detent::detail::evaluate(BoolDef, ...)` (`include/detent/detail/evaluation.hh:10`), used by `Client::eval` (`client.cc:199`), `ClientReadView` on a `Snapshot` (`read_view.cc:30`) and the defaults fallback.
2. `ReadView::try_eval` (`view.cc:373`): the mapped-view evaluator; stack buffer `qualified[200]` for the hash input, no heap allocation on a hit.
3. `ShmReader::eval` (`shm_reader.cc:191-232`): the legacy shm evaluator (deleted with item 4).

The managed one (2) already runs directly over the mapped view without a `Snapshot`; it is the one to keep. Precedence (switch override forced bool or rollout, then global rollout, then default; missing hash returns the default) is identical in all three, so the cut deletes 1 (once defaults are settled) and 3.

### Per-read work today (static reading, not measured)

In-process lookup on a steady-state `Client` with a `LocalReadSource` (the path an app takes):
1. `Client::eval` (`client.cc:164-166`): `read_source_->read_view()` is a virtual call plus `atomic_shared_ptr::load()` (`client_source.cc:152`, one refcount increment and decrement), builds a `ClientReadView` (three `shared_ptr` members), then a second virtual call `EvaluationView::eval`, then `count_local_read` (relaxed atomic increment, `client.cc:28`).
2. `FrozenEvaluation::eval` (`client_source.cc:41-47`): null-checks the override table, `name.find(':')`, calls `ReadView::try_eval`.
3. `ReadView::try_eval` -> `root_->definition(ns,key)` walks the page trie. Each page goes through `LazyPageAccess::resolve` (`lazy_page_access.cc:132-133`, `std::lock_guard lock(mutex)` at `:133`): **`std::lock_guard` on one `std::mutex` shared by every reader of the generation**, a `maps.find` and a `checked.find` hash lookup, then returns the span. A page seen for the first time is checksum-verified and definition-validated and inserted into the LRU (`:154-166`, allocation from the storage account); a segment seen for the first time costs `openat + fstat + fstatat + mmap` (`open_mapping`, `:89-117`).
4. Heap allocation on a hit, steady state: none found; on a miss or wrong type: one `KnobError` with `std::string` name and detail (`view.cc:25`).
5. Syscalls on a steady-state hit: none. A background thread (`client_source.cc:116-129`) wakes every `poll_interval` (200 ms) and runs `GenerationReader::refresh()` (reads the `selected` descriptor and checks the lease): a few `openat`/`fstat`/`read` calls per tick, off the read path.
6. `detent get` (process per lookup) adds: process start, config file reads, `ReaderCapacity`, the prepared attach syscalls above, first-touch `openat/fstat/fstatat/mmap` per segment, checksum verification of every page on the root-to-leaf path, the double lookup for ints, and, when the managed agent is not running and the config is set, `launch_agent` (fork, exec, wait; `agent/main.cc:330`). The exact syscall count needs `strace -c` under `quiet strict`; I did not run it.

The shared mutex is the thing a "very fast, thin" reader must remove: it serializes all threads at every page.

## Proposed reader shape

One implementation, a C ABI, every language a wrapper.

```text
detent_reader.h (C ABI, ~10 functions)        implemented once in C++ on detent_local_view
  detent_open(dir, publisher_uid, flags, &reader)      -> status   (map, validate, pin)
  detent_refresh(reader)                               -> changed? (optional, caller's thread)
  detent_eval(reader, name, hash, hash_len, group, group_len, &out) -> status
  detent_get(reader, name, group?, &out)               -> status
  detent_status(reader, &cursor, &counts)  ;  detent_close(reader)
C++ : header-only inline wrapper     Python : cffi/ctypes or pybind11 thin    Rust : bindgen + safe wrapper
```

Shape rules:
1. **One evaluator**: `ReadView::try_eval/try_get` over the mapped view (`view.cc:346-413`). No `Snapshot`, no `Client`, no `EvaluationView` virtual hop, no `std::function` factory. A reader owns a pinned generation; `detent_refresh` swaps it (RCU: pointer swap, old generation retired by refcount), called by the caller on a thread of its choosing or never (apps that restart on config change). No hidden thread unless the caller asks for one.
2. **Error codes, not `KnobError`**: `status` is an enum (ok, missing, wrong-type, invalid-name, source-failure); the message is produced only on request. Removes the string allocation on the miss path and the bool-then-int double lookup (`detent_get_any(name, &kind, &value)` returns the type with the value in one descent).
3. **No lock on a hit**: once a generation's pages are verified at attach (prepared metadata already carries the proof, `docs/PREPARED_ACTIVATION_ARCHITECTURE.md`), per-page `resolve` should be a bounds check on an immutable mapping. If lazy verification stays, replace the mutex with a per-page atomic "verified" byte. This is a design question for the performance engineer, not decided here; the 5M-record sub-millisecond first-read gate (`docs/status/README.md`) is the constraint to preserve.
4. **Zero syscalls and zero allocations per lookup** as the stated contract, checked by the existing allocation probe (`tests/support/allocation_probe.hh`) and a syscall-count test.
5. Baked defaults and per-process overlays (`name=value`) are an optional small struct passed at open (ASK 2); the C ABI takes them as arrays, not as JSON.
6. Python and Rust wrap the C ABI (no pybind11 over `Client`, no cxx shim over `InitOptions`); `detent get` becomes a ~100-line C++ main over the same header. The existing `LocalReadSource`/`GenerationReader` logic is the implementation to extract, not rewrite.
7. Measure before and after with the existing read benchmarks (`benchmarks/detent_read_bench.cc`, `prepared_client_probe.cc`): the README numbers (6.71M ops/s `Client`, 36.2M ops/s shm) were for the JSON/shm designs, not the mapped generation path, so a baseline of the current mapped path is missing and should be taken first.

## ASK items

1. **File source and baked defaults JSON.** Do apps keep a baked-defaults file (`just_knobs.json`, `schema_version` + `knobs`, spec section 4) as the fallback under the mapped generation, or does the reader error on a missing knob and take per-read `--default` values only? Keeping it keeps `parse_defaults` and `Snapshot` as a defaults carrier (QUICKSTART, spec, `detent_demo`, and `FrozenEvaluation::fallback` depend on it); dropping it deletes `Snapshot`, `json_schema.cc` public entry points and the fallback evaluator, and the reader needs no JSON at all. Recommendation: drop it; keep only CLI-style overlays and a per-call fallback in the language wrappers.
2. **CLI overlays (`name=value`) in the reader.** Keep `OverrideTable` as a first-class feature or drop it with `detent::Client`? Recommendation: keep as an optional argument at open.
3. **`reader_validation: "full"` and `--reader-validation`.** Full validation of a paged image is an alternate, slower mode created for flat and untrusted compatibility (`PRODUCTION_CONTRACT.md:27`). Keep as a security option for untrusted publishers, or delete and keep prepared only? It is not legacy format code, so it is the owner's call.
4. **`detent::global` free functions** (`global.hh`, "pyjk-style compatibility", 122 lines): keep as the API of the new C++ reader, or delete. Recommendation: delete; the reader handle replaces it.

## Cut order

Independent of each other (any order, no breakage between them):
A. Delete `tests/e2e/` (574, unreferenced).
B. Delete `examples/publish_snapshot.cc` and fix `examples/CMakeLists.txt` comment.
C. Delete `detent-authority-admin` import/export, `admin_runtime`, `test_migration.sh` (292), and its website pages (it is the tool's only function; the target and its CMake guard go with it).
D. Move `kLoopbackHost` out of `include/detent_http/address.hh` into `detent_cli` (prerequisite for E and G, no behaviour change).

Must go together:
E. Classic distributor + classic Raft + their tests + `detentd_fanout_bench` + `publication_bench` + `test_agent_http.sh` + `DETENT_BUILD_DISTRIBUTOR` rename/removal + `cluster` and `generation_cache` pins + `main.cc` rewrite to authority-only + a plain-HTTP authority health probe for `--status`. One change; splitting leaves `main.cc` calling a deleted `Distributor` or the authority gated off.
F. `detent_http` library and tests: removable only with E and with the agent change G (the agent links it), or after E if G lands in the same series.
G. Legacy agent profile: unmanaged `detent-agent`, shm writer/reader, `agent.cc`, `detent get` shm path, `detent_agent_lib` split, shm tests and benchmarks. Must precede or accompany F and the `Client` updater removal (`Agent` uses `Client::on_update`, `source_poll`).
H. `Client` updater/file/poll source/`json_patch`/`SnapshotPatch`/JSON snapshot entry points, then the new C ABI reader and the Python/Rust/`detent get` rewrite. Needs E, F, G first (their last users). Between G and H the bindings and `detent_demo` still compile against `Client`; they break only when H removes the file source, so land the reader and ported bindings in the same change as the removal.
I. Flat-v1: after fixtures move to paged (`snapshot_fixture.hh` and 26 test files), delete `ImageBuilder`/flat validators/`UpgradeLegacy`/`image_format`. Independent of E-H but the largest, so last.
J. Docs and website pass with each step (list below); the stale `flat-v1 (default)` line in `configuration.md:103` can be fixed first.

What breaks in between: after D+E alone, `detent-agent` still builds (it uses `detent_http` until F/G); after G without F, `detent_http` has no consumers but still builds; after H before the reader exists, bindings fail; after I nothing else changes.

## W2 (`phase/w2-s2`) sequencing

Files W2 touches that this plan deletes or rewrites: `src/detentd/detentd.cc`, `detentd.hh`, `main.cc` (E; main.cc is rewritten, not deleted), `tests/detentd/*` including the new `test_distributor_owner.cc`, `test_raft_application_retry.cc`, `test_raft_cluster.cc`, `test_cli_defaults.sh`, `CMakeLists.txt`, `benchmarks/detentd_fanout_bench.cc`, `benchmarks/container-alternative/publication_bench.cc`, `docs/mechanisms/classic-publish.md`, `docs/USAGE.md`, `website/.../reference/cli/detentd.md`, `website/.../configuration.md`, `tests/detent_agent/test_agent_http.sh`. Files W2 touches that stay: `src/detent_authority/service.cc/.hh` (442 lines), `src/detent_relay/*`, `src/detent_cli/config.*`, `src/detent_distribution_http/session_request.*`, `tests/detent_authority/*`, `tests/support/handler_harness.hh`, `tests/detent_delivery/*` fixtures, `.github/workflows/ci.yml`, `include/detent/assert_off_loop.hh`.

Recommendation: land W2 first in its authority parts and let E delete the classic parts on top of W2, since a rebase of E over W2's `main.cc` and `detentd.cc` is a delete-delete conflict at worst, whereas the reverse makes W2 rebase its 556-line `detentd.cc` rewrite into nothing. Better: tell W2 now to stop work on the classic owner (S3, `classic-publish.md`, `test_distributor_owner`, the Raft apply decision) and land only the authority, relay and `main.cc` pieces. `main.cc`, `config.cc` (`src/detent_cli`) and `service.cc` are touched by both W2 and E/D/H, so C, D and any `main.cc` edit wait for W2's merge.

## Docs and website pages that mention these modes

Counts are matching lines (`v1/snapshot`, `detent_http`, `--cluster`, `cluster::`, multicast, seqlock, `snapshot.json`, `detentd.json`, `flat-v1`, classic, legacy, `DETENT_BUILD_DISTRIBUTOR`, `source-address`, `detent_publish`, `json_patch`, `If-None-Match`, `schema_version`); history under `docs/reviews/` and `docs/measurements/` is excluded (records, not live docs).

Top level: `README.md` 51, `QUICKSTART.md` 18, `justknobs-implementation-spec.md` 3, `AGENTS.md` (check), `CHANGELOG.md` (check; history).

`docs/` (live): `USAGE.md` 42, `LIBRARY_BOUNDARIES.md` 34, `design/http-coroutines.md` 24, `IMPLEMENTATION.md` 21, `ACTORS.md` 18, `DEPENDENCY_UPGRADES.md` 12, `VALIDATED_ARCHITECTURE.md` 11, `AUTHORITY_ARCHITECTURE.md` 10, `LOCAL_IMAGE_FORMAT.md` 9, `DISK_LIFECYCLE_ARCHITECTURE.md` 8, `DISK_STORE_INTEGRATION.md` 8, `SCALING.md` 8, `DELIVERY_INTEGRATION_ARCHITECTURE.md` 6, `RELAY_ARCHITECTURE.md` 6, `AUDIT.md` 5, `INCREMENTAL_PUBLICATION_ARCHITECTURE.md` 5, `SUBSCRIPTIONS_ARCHITECTURE.md` 5, `DISTRIBUTION_ARCHITECTURE.md` 4, `DOCUMENTATION_FINDINGS.md` 4, `LIBRARY_ADOPTION_ARCHITECTURE.md` 4, `READER_CAPSULE_ARCHITECTURE.md` 4, `RESOURCE_EXTRACTION_ARCHITECTURE.md` 4, `SERVICES.md` 4, plus 1-3 each in `AUTHORITY_COMPONENT_REVIEW`, `AUTHORITY_EXPORT`, `AUTHORITY_READINESS`, `AUTHORITY_TRANSPORT`, `BTREE_EXCEPTION_SAFETY_ARCHITECTURE`, `BTREE_OWNERSHIP_MOVE_ARCHITECTURE`, `CAPACITY_QUALIFICATION`, `CAPACITY_RUNTIME`, `COMPLETION_ALLOCATION_ARCHITECTURE`, `CONSOLIDATION_RESPONSE`, `CONTAINER_SELECTION_*`, `HTTP_CLIENT_EXTRACTION_ARCHITECTURE`, `HTTP_LOGGING_ARCHITECTURE`, `LINUX_COMPLETION`, `LOGGING`, `MANAGED_FAILOVER_ARCHITECTURE`, `ONLINE_CAPACITY_ARCHITECTURE`, `PAGED_DELIVERY_REVIEW`, `PLAN`, `PREPARED_ACTIVATION_ARCHITECTURE`, `PRODUCTION_CONTRACT`, `READER_CAPACITY_ARCHITECTURE`, `RELAY`, `RELEASE_PLAN`, `RESTORE_ARCHITECTURE`, `SCALE_TARGETS`, `SHARED_LIBRARY_DECISIONS`, `SHARED_MEMORY_PADDING`, `SNAPSHOT_TRANSFER_ARCHITECTURE`, `STAGE6_ARCHITECTURE`, `mechanisms/distributor-publication.md`, `handoffs/http-clients.md`, `status/{actors-docs,http-coroutines,reader-acl,README}.md`. Delete outright: `src/detentd/CLUSTERING.md`, `docs/SHARED_MEMORY_PADDING.md` (shm-only; confirm), `docs/mechanisms/distributor-publication.md` (classic), and on W2 `docs/mechanisms/classic-publish.md`.

Diagrams: `diagrams/01_overview.d2`, `02_distributor_internals.d2`, `03_gossip.d2` (classic Raft), `06_adapters.d2`, `07_agent.d2`, `08_detent_deps.d2` (9), `09_thirdparty_deps.d2`, and their SVGs.

Website (`website/src/content/docs/`): delete `reference/http-api/snapshot.md` (13), `reference/snapshot-format.md` (6), `architecture/distributor.md` (8), `architecture/raft.md` (5), `guides/clustering.md` (12), `operations/clustering.md` (7), `guides/live-updates.md` (12), `reference/cli/detent-authority-admin.md` (5), `reference/cpp-api/sources.md` (5, rewrite for the reader). Rewrite: `architecture.md` 11, `reference/cli/detentd.md` 10, `rollouts.md` 8, `settings-and-flags.md` 8, `guides/durable-authority.md` 7, `guides/kill-switches.md` 7, `architecture/agent.md` 6, `architecture/authority.md` 6, `architecture/dependencies.md` 6, `reference/configuration.md` 6 (also stale flat-v1 default), `guides/shell-and-agent.md` 5, `operations/authority.md` 5, `operations/upgrading.md` 5, `reference/http-api.md` 4, `reference/cli/detent-agent.md` 4, `guides/cpp-application.md` 4, `group-overrides.md` 4, `installation.md` 4, `quickstart.md` 4, `operations/{logging,security}.md` 3, `guides/managed-delivery.md` 3, and 1-2 each in `index.mdx`, `welcome.md`, `faq.md`, `benchmarks.md`, `roadmap.md`, `reference/{cli,cli/detent,cpp-api,errors,limits}.md`, `reference/http-api/authority.md`, `operations/services.md`, `architecture/{delivery,design-documents}.md`; sidebar `website/src/lib/sidebar.mjs`.

Packaging text: `packaging/homebrew/detent.rb`, `packaging/launchd/org.detent.detentd.plist`, `packaging/rc.d/detentd`, `packaging/systemd/detentd.service` (one mention each; check the arguments they pass).
