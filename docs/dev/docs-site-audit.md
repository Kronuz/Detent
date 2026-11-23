# Docs-site audit

> Status: audit of 2026-11-10 (quest `docs-site-audit`, D52). Read-only over every page: no page was changed.
> Audited tree: `main` at `5b50eb3` (`src/`, `include/`, `tools/`, `bindings/`, `packaging/`, `.github/`, `docs/`); the site is `website/src/content/docs/` (62 pages), plus `README.md`, `QUICKSTART.md` and the 76 `docs/*.md` files.
> Source of truth: the code on `main`, then `docs/mechanisms/`, `docs/design/` and `docs/status/`. Campaign decisions D43, D44, D47 and D52 in `.campaign/detent/PLAN.md`.
> Nothing was built or run: a claim only a running system settles is under "## Unverified" with its probe.

## Summary

62 site pages: current 11, stale 7, wrong 21, missing content 22, delete 1. `README.md`: wrong. `QUICKSTART.md`: current. The 76 `docs/*.md` files: current 39, stale 30, wrong 4, missing content 2, delete 1.

Verdicts: `current` every claim is true on `main` and nothing important is missing; `stale` was true of an earlier Detent or describes an old shape; `wrong` contradicts the code on `main`; `missing content` is true but lacks what the code serves; `delete` documents only removed features or duplicates another page. A verdict is by what is true on `main` today. Rows that say `OWNED <quest>` name text a not-yet-landed quest changes, so the area quests re-check them after it lands (see "## Owned by other quests").

What the audit found, in order of weight:

1. `detent::Client`, `InitOptions`, `detent::global`, `TestKnobs`, the C++ `--defaults` file and the C++ file and managed sources are still in the tree on `main` (`include/detent/client.hh:121`, `include/detent/client.hh:59`, `src/detent_agent/main.cc:235`), because `legacy-h` is blocked and `reader-s6` has not landed: there is no `Reader`, no `libdetent_reader` and no `detent_*` C ABI on `main` yet. Pages that describe the Client are true of `main` and are marked `OWNED legacy-h`: they are rewritten around `Reader` when it lands, and 51 rows depend on it. Writing the area quests before `legacy-h` lands means writing the Client pages twice (see "## Owned by other quests").
2. Pages that are wrong on `main` independently of those quests: the image format (`delivery.md` documents the flat v1 image as the generation format, and `reference/configuration.md:99` and `guides/managed-delivery.md:149` give `flat-v1` as the `image_format` default where the code default is `PagedV2`, `include/detent_delivery/coordinator.hh:30`); `architecture/update-path.md` (candidate back-off and an HTTP source that do not exist); `reference/configuration.md:5` (claims nothing needs configuring: `detent get`, `detent-agent` and `detentd` all fail without a source or `--authority`); `reference/cli/detent.md` (the `ERR` line goes to stderr, `src/detent_agent/main.cc:298`); `operations/authority.md` (voters "3 or 5", the code accepts 3, 5 or 7); `operations/security.md` (one claim of a single-link check that only the managed credential file has).
3. Whole behaviors the code serves and no page documents: `detent-relay` (a reference page, `relay.json`, its health endpoint), `publisher_user`, `reader_validation`, ten `reader_*` settings and their flags and environment variables (only `docs/USAGE.md:250-273`), `SourceFailure` and 14 HTTP error names, `storage_backend`, the relay and authority route set, the `detent-agent --status` output.
4. `benchmarks.md` and `architecture/read-path.md` treat 149.1 ns/op as per-read latency where the harness reports aggregate wall time per operation across four threads; the footprint table measures the removed unmanaged agent. Every figure on those pages not carried by a committed measurement file is under "## Unverified".
5. D47 #26 cannot be written yet: `voters[].host` accepts IP literals only (`src/detent_authority/service.cc:381`, `asio::ip::make_address`), so the configuration reference must not say voters may be DNS names until a quest changes the loader (see "## Inputs"). `--upgrade-managed` (`StoreOpenMode::UpgradeLegacy`, `src/detent_agent/main.cc:116`) is a legacy-adoption mode D43 forbids and no quest removes (see "## Code questions the audit raised").
6. Site infrastructure: every page is in the sidebar; one internal link is broken (see "## Site infrastructure").


## Page table

One row per page. "Owned" names the not-yet-landed quests that change the page (see "## Owned by other quests"). Detail rows follow in "## Pages".

| Page | Area | Verdict | Owned |
|---|---|---|---|
| `architecture.md` | 1 | current | legacy-h, reader-s6, observability |
| `architecture/agent.md` | 1 | missing content | legacy-h, reader-s7-s9, observability |
| `architecture/authority.md` | 1 | missing content | async-drivers, daemon-ready-leader |
| `architecture/delivery.md` | 1 | wrong | legacy-i, dense-pages |
| `architecture/dependencies.md` | 1 | wrong | legacy-h |
| `architecture/design-documents.md` | 1 | stale | legacy-i, dense-pages |
| `architecture/how-it-works.md` | 1 | stale | dense-pages, reader-s6, reader-s7-s9, namespace-s3b, namespace-s4-s10, legacy-h |
| `architecture/read-path.md` | 1 | wrong | legacy-h |
| `architecture/relay.md` | 1 | missing content | namespace-s4-s10, observability |
| `architecture/update-path.md` | 1 | wrong | legacy-h |
| `about.md` | 1 | current | legacy-h, reader-s6, reader-s7-s9 |
| `benchmarks.md` | 1 | wrong | legacy-i, legacy-h, observability |
| `index.mdx` | 2 | current | legacy-h, reader-s7-s9 |
| `welcome.md` | 2 | wrong | legacy-h, reader-s6 |
| `basics.md` | 2 | wrong | legacy-h, reader-s7-s9, dense-pages |
| `installation.md` | 2 | wrong | rc5, legacy-h |
| `quickstart.md` | 2 | current | daemon-ready-leader |
| `settings-and-flags.md` | 2 | missing content | legacy-h, reader-s7-s9 |
| `rollouts.md` | 2 | current | legacy-h |
| `group-overrides.md` | 2 | missing content | legacy-h |
| `README.md` | 2 | wrong | legacy-h, legacy-i, dense-pages, reader-s6, reader-s7-s9, rc5, observability |
| `QUICKSTART.md` | 2 | current | daemon-ready-leader, observability |
| `guides.md` | 3 | wrong | legacy-h, reader-s6 |
| `guides/cpp-application.md` | 3 | missing content | legacy-h, reader-s6 |
| `guides/durable-authority.md` | 3 | missing content | daemon-ready-leader |
| `guides/durable-relay.md` | 3 | missing content | namespace-s4-s10, observability |
| `guides/kill-switches.md` | 3 | current | legacy-h, observability |
| `guides/live-updates.md` | 3 | missing content | legacy-h |
| `guides/managed-delivery.md` | 3 | wrong | legacy-i, dense-pages, legacy-h, reader-s6, namespace-s3b |
| `guides/python-and-rust.md` | 3 | stale | legacy-h |
| `guides/shell-and-agent.md` | 3 | missing content | legacy-h, reader-s6, reader-s7-s9, observability |
| `guides/testing.md` | 3 | stale | legacy-h |
| `operations/authority.md` | 3 | missing content | rc5-release-candidate |
| `operations/logging.md` | 3 | wrong |  |
| `operations/security.md` | 3 | wrong | rc5-release-candidate, legacy-h |
| `operations/services.md` | 3 | missing content |  |
| `operations/upgrading.md` | 3 | delete | dense-pages, legacy-i, legacy-h |
| `reference/configuration.md` | 4 | wrong | legacy-h, legacy-i, dense-pages, rc5 |
| `reference/limits.md` | 4 | missing content | legacy-h, dense-pages, legacy-i, rc3 |
| `reference/cli.md` | 5 | missing content | daemon-ready-leader |
| `reference/cli/detent.md` | 5 | wrong | legacy-h, reader-s7-s9 |
| `reference/cli/detentd.md` | 5 | missing content | daemon-ready-leader |
| `reference/cli/detent-agent.md` | 5 | stale | legacy-h, reader-s7-s9 |
| `reference/knobs.md` | 5 | current | legacy-h |
| `reference/errors.md` | 5 | missing content | legacy-h, namespace-s3b |
| `reference/observability.md` | 5 | missing content | legacy-h, observability |
| `reference/cpp-api.md` | 5 | missing content | legacy-h |
| `reference/cpp-api/client.md` | 5 | wrong | legacy-h |
| `reference/cpp-api/global-and-testing.md` | 5 | stale | legacy-h |
| `reference/cpp-api/sources.md` | 5 | stale | legacy-h |
| `reference/bindings.md` | 5 | current | legacy-h, reader-s7-s9 |
| `reference/bindings/python.md` | 5 | current | legacy-h, reader-s7-s9 |
| `reference/bindings/rust.md` | 5 | current | legacy-h, reader-s7-s9 |
| `reference/evaluation.md` | 5 | missing content | legacy-h |
| `reference/http-api.md` | 5 | wrong |  |
| `reference/http-api/authority.md` | 5 | wrong |  |
| `reference/http-api/health.md` | 5 | missing content |  |
| `reference/snapshot-format.md` | 5 | wrong | dense-pages (conditional) |
| `faq.md` | 5 | wrong | legacy-h, reader-s7-s9 |
| `help.md` | 5 | current |  |
| `next.md` | 5 | wrong | legacy-h, reader-s6 |
| `roadmap.md` | 5 | wrong | legacy-h, legacy-i, dense-pages, reader-s6, reader-s7-s9, namespace-s3b, namespace-s4-s10, async-drivers, observability, rc1-rc5 |
| `contributing.md` | 5 | missing content | docs-e2e |
| `copyright.md` | 5 | missing content |  |

### docs/*.md

| File | Verdict | Owned |
|---|---|---|
| `docs/ACTORS.md` | missing content | legacy-h, legacy-i, dense-pages, namespace-s3b |
| `docs/ASYNC_AUTHORITY_ARCHITECTURE.md` | stale | async-drivers |
| `docs/AUDIT.md` | stale |  |
| `docs/AUTHORITY_ARCHITECTURE.md` | stale | async-drivers |
| `docs/AUTHORITY_COMPONENT_REVIEW.md` | current |  |
| `docs/AUTHORITY_EXPORT.md` | stale |  |
| `docs/AUTHORITY_READINESS.md` | stale |  |
| `docs/AUTHORITY_SCHEDULING_ARCHITECTURE.md` | current | async-drivers |
| `docs/AUTHORITY_TRANSPORT.md` | stale |  |
| `docs/BOUNDED_APPLICATION.md` | current |  |
| `docs/BTREE_COMPACT_ROOT_ARCHITECTURE.md` | current |  |
| `docs/BTREE_ERASE_ARCHITECTURE.md` | stale |  |
| `docs/BTREE_EXCEPTION_SAFETY_ARCHITECTURE.md` | current |  |
| `docs/BTREE_NONFULL_INSERTION_ARCHITECTURE.md` | current |  |
| `docs/BTREE_OWNERSHIP_MOVE_ARCHITECTURE.md` | stale |  |
| `docs/BTREE_RELEASE_RISK.md` | current |  |
| `docs/BTREE_STRUCTURAL_INSERTION_ARCHITECTURE.md` | stale |  |
| `docs/CAPACITY_QUALIFICATION.md` | stale | legacy-i, dense-pages |
| `docs/CAPACITY_RUNTIME.md` | current | legacy-i, dense-pages |
| `docs/CHECKPOINT_POLICY.md` | current |  |
| `docs/COMPLETION_ALLOCATION_ARCHITECTURE.md` | current |  |
| `docs/CONSOLIDATION_RESPONSE.md` | current |  |
| `docs/CONTAINER_SELECTION_ARCHITECTURE.md` | current |  |
| `docs/CONTAINER_SELECTION_REPORT.md` | stale |  |
| `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md` | stale | legacy-h, reader-s6 |
| `docs/DEPENDENCIES.md` | current |  |
| `docs/DEPENDENCY_UPGRADES.md` | stale |  |
| `docs/DISK_LIFECYCLE_ARCHITECTURE.md` | stale |  |
| `docs/DISK_STORE_INTEGRATION.md` | stale |  |
| `docs/DISTRIBUTION_ARCHITECTURE.md` | stale | legacy-i, legacy-h, dense-pages |
| `docs/DISTRIBUTION_CODEC.md` | current |  |
| `docs/DOCUMENTATION_FINDINGS.md` | stale | legacy-h |
| `docs/EXTRACTION_CHECKLIST.md` | stale |  |
| `docs/FAST_CLIENT_ARCHITECTURE.md` | current | legacy-h, dense-pages |
| `docs/FLEET_QUALIFICATION_ARCHITECTURE.md` | current |  |
| `docs/FLEET_QUALIFICATION.md` | current | legacy-h |
| `docs/HOW_IT_WORKS.md` | stale | legacy-h, reader-s6, namespace-s3b, namespace-s4-s10 |
| `docs/HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md` | current | legacy-h |
| `docs/HTTP_LOGGING_ARCHITECTURE.md` | stale |  |
| `docs/IMPLEMENTATION.md` | stale | legacy-i, dense-pages |
| `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md` | stale | dense-pages, legacy-i |
| `docs/LIBRARY_ADOPTION_ARCHITECTURE.md` | current |  |
| `docs/LIBRARY_BOUNDARIES.md` | current |  |
| `docs/LINUX_COMPLETION.md` | current |  |
| `docs/LOCAL_IMAGE_FORMAT.md` | wrong | legacy-i, dense-pages |
| `docs/LOGGING.md` | wrong | observability |
| `docs/MANAGED_FAILOVER_ARCHITECTURE.md` | current |  |
| `docs/MSGPACK_EXTRACTION_ARCHITECTURE.md` | current |  |
| `docs/ONLINE_CAPACITY_ARCHITECTURE.md` | current |  |
| `docs/PAGED_DELIVERY_REVIEW.md` | stale | legacy-i, dense-pages |
| `docs/PERSISTENT_MAP_EXTRACTION_ARCHITECTURE.md` | current |  |
| `docs/PERSISTENT_TRANSPORT_ARCHITECTURE.md` | current |  |
| `docs/PLAN.md` | current |  |
| `docs/PREPARED_ACTIVATION_ARCHITECTURE.md` | stale | legacy-i |
| `docs/PREPARED_PACK_ACCESS_ARCHITECTURE.md` | current |  |
| `docs/PRODUCTION_CONTRACT.md` | stale | legacy-i, dense-pages, legacy-h |
| `docs/READER_CAPACITY_ARCHITECTURE.md` | stale |  |
| `docs/READER_CAPSULE_ARCHITECTURE.md` | stale |  |
| `docs/RECOVERY_RECLAMATION_ARCHITECTURE.md` | current |  |
| `docs/RELAY_ARCHITECTURE.md` | current |  |
| `docs/RELAY.md` | missing content | legacy-h |
| `docs/RELEASE_PLAN.md` | stale | rc1-rc5 |
| `docs/RESOURCE_EXTRACTION_ARCHITECTURE.md` | current |  |
| `docs/RESTORE_ARCHITECTURE.md` | current | rc4 |
| `docs/RETAINED_STATE_ARCHITECTURE.md` | current |  |
| `docs/SCALE_TARGETS.md` | wrong | legacy-h |
| `docs/SCALING.md` | wrong | legacy-h |
| `docs/SERVICES.md` | stale | legacy-h, reader-s7-s9 |
| `docs/SHARED_LIBRARY_DECISIONS.md` | current |  |
| `docs/SHARED_MEMORY_PADDING.md` | delete |  |
| `docs/SNAPSHOT_TRANSFER_ARCHITECTURE.md` | current |  |
| `docs/STAGE6_ARCHITECTURE.md` | current |  |
| `docs/STORAGE_BARRIER_ARCHITECTURE.md` | current |  |
| `docs/SUBSCRIPTIONS_ARCHITECTURE.md` | current |  |
| `docs/USAGE.md` | stale | legacy-h, legacy-i, reader-s6, reader-s7-s9 |
| `docs/VALIDATED_ARCHITECTURE.md` | current |  |


`README.md`: **wrong** (OWNED legacy-h, legacy-i, dense-pages, reader-s6, reader-s7-s9, rc5, observability). `QUICKSTART.md`: **current** (OWNED daemon-ready-leader, observability). Areas: 1 architecture, 2 features and quick start, 3 usage guides and operations, 4 configuration and limits, 5 reference and the rest (the quest files assign `reference/cli*`, `knobs`, `errors`, `observability`, the API, bindings, evaluation, HTTP and format pages, `faq`, `help`, `roadmap`, `next`, `contributing`, `copyright` and `docs/*.md` to area 5; `welcome.md` and `README.md` and `QUICKSTART.md` are placed in area 2 and `welcome.md` is in no quest file's list, see "## Code questions the audit raised").


## Pages

`ROOT` below is the repository root. Format: path, verdict, why; then one line per stale or wrong claim (`L<line>` is the page line): `"quote" -> what is true: file:line`. `MISSING` lines name what the code serves that the page lacks. `file:line` is in this repository at `main` unless a repository is named. A claim that is true needs no row. Blocks for `docs/*.md` are triage rows (the files are internal records, not site pages).


### 1. Architecture

- `architecture.md` | **current** | Every claim holds on main today; it is Client-centric (`detent::Client`, file source, `selected` poll), so it is rewritten when the Client goes. OWNED legacy-h, reader-s6
  - L24 "App process\n(detent::Client)" and L49 "Your app (`detent::Client`)" -> exists on main: include/detent/client.hh (class Client); rewrite when it is deleted.
  - L75-80 "One background thread per client ... With a file source, it fetches candidates..." -> true today: src/detent/client.cc:130-141 (file/poll updater thread), include/detent_local/client_source.hh:10,31 (200 ms `selected` poll); removed with the Client.
  - MISSING: the Python and Rust bindings as the non-C++ read path: bindings/python/detent_python.cc, bindings/rust/src/lib.rs (both wrap Client today; move to the reader core under legacy-h).
  - MISSING: relay is not built by default and needs OpenSSL 3.0 (root CMakeLists.txt option DETENT_BUILD_RELAY OFF; src/CMakeLists.txt:95).
  - MISSING: the observability plane (`detent_usage`, `detent-agent --status --usage`, usage page): designed in docs/design/observability.md, not on main. OWNED observability

- `architecture/agent.md` | **missing content** | Claims hold; the page omits the flags, config keys, env vars, status output and the signal behavior the code serves. OWNED legacy-h, reader-s7-s9, observability
  - L12 "`detent get` and the C++ managed source read those generations" -> true on main: src/detent_agent/main.cc:230-300 (`cmd_managed_get` builds a `detent::Client` with `InitOptions`, `--defaults`); include/detent_local/client_source.hh:34 (`make_managed_read_source`). Rewrite when legacy-h lands; `detent get` moves to the reader core under reader-s7-s9.
  - L14 "exits with `no source configured`" -> true: src/detent_agent/main.cc:105 (agent), :305 (`detent get`).
  - L52 "SIGTERM drains logs and exits cleanly" -> imprecise: SIGINT and SIGTERM both close the managed coordinator and the agent exits once it is drained: src/detent_agent/managed_host.cc:183-186,195-203. Logs are not the thing drained.
  - MISSING: flags `--daemonize`, `--system`, `--runtime-dir`, `--config`, `--generation-dir`, `--upgrade-managed` (mutually exclusive with `--initialize-managed`), `--publisher-user`, `--reader-validation prepared|full`, `--reader-*` capacity limits, `--generation-cluster/-group/-epoch/-scope`: src/detent_agent/main.cc:99-103,154-167,325-340.
  - MISSING: `agent.json` keys (defaults_path, runtime_dir, managed_path, generation_dir, publisher_user), env vars DETENT_RUNTIME_DIR / DETENT_MANAGED_CONFIG / DETENT_GENERATION_DIR / DETENT_DEFAULTS_PATH, default state dir (`default_agent_state_dir`): src/detent_agent/main.cc:340-358,408-411.
  - MISSING: `--status` output (`running (pid N)` / `not running`, `durable revision=... knob_count=...`, exit 1 when not running): src/detent_agent/main.cc:125-145.
  - MISSING: managed.json keys the agent honors (compaction_interval_ms, scrub_bytes_per_second, heap_bytes, mapping_bytes, disk_*, quorum_refresh_ms, delivery_mode, compression): src/detent_delivery/configuration.cc:102-131.
  - MISSING: `--status --usage` and the Prometheus textfile (designed, not built). OWNED observability

- `architecture/authority.md` | **missing content** | Mechanism prose holds; the page lacks the routes, status/error names, config keys and bind rules the code serves; two numeric claims are Unverified. OWNED async-drivers, daemon-ready-leader
  - L96 "The authority owner runs one event loop and never blocks it on `fsync`" -> the owner loop and the Worker callback API are replaced by coroutine drivers under async-drivers; reread when it lands. OWNED async-drivers
  - L121-129 "Where It Runs" -> true: src/detent_authority/service.cc:208 (config keys), :420-423 (`publisher_bind` loopback default; non-loopback requires `publisher_tls`), :373-376 (3/5/7 odd voters), :1664 (existing `manifest` recovery); src/detentd/main.cc usage text (port 8317, `detentd.json`).
  - L82 "Membership is a single voter, or a fixed odd list of 3, 5 or 7" -> true: src/detent_authority/service.cc:373-376.
  - L64 "last 256 results of the current generation" -> true: src/detent_authority/state.hh:29 (`results = 256`).
  - L113-115 "73.2 ms to 3.7 ms" -> verified: benchmarks/application-quanta-baseline.json:10,34.
  - MISSING: routes `POST /v2/open`, `/v2/close`, `/v2/transactions`, `/v2/read`, `/v2/result`, `POST /v2/distribution`, `POST /v2/distribution/session/open|next|close`, `GET /v1/health`: src/detent_authority/service.cc:775-827,1066,1085; src/detent_distribution_http/session_request.cc:84-88.
  - MISSING: result names `invalid, applied, no_change, conflict, ... unknown_session, session_fenced, out_of_order, result_unavailable`: src/detent_authority/service.cc:153-154.
  - MISSING: config keys `publisher_tls`, `publisher_bind`, `publisher_name`, `storage_backend` (`auto|native|fallback`), `consensus_limits`, `peer_tls`, `voters` (id, host, port, certificate_sha256), `node`: src/detent_authority/service.cc:208,341,360-390; docs/LINUX_COMPLETION.md:13.
  - MISSING: `detentd --status`, `--daemonize`, `--reactors`, `--system`; `DETENTD_PORT/REACTORS/RUNTIME_DIR`: src/detentd/main.cc usage text. `detentd --daemonize` returns before a leader exists today. OWNED daemon-ready-leader

- `architecture/delivery.md` | **wrong** | "Immutable Generations" describes the flat v1 image as if it were the format; the default is paged (root, pages, packs). OWNED legacy-i, dense-pages
  - L53-65 "Each generation is a single image file: 192-byte header, 24-byte namespace rows, 48-byte record rows, 24-byte override rows, one dense string region" -> that is `flat-v1` (include/detent_local/layout.hh:8 header_bytes=192, namespace_bytes=24, record_bytes=48); the default is `LocalImageFormat::PagedV2` (include/detent_delivery/coordinator.hh:30): a 320-byte root, 64 KiB max pages, 1 MiB packs (include/detent_local/paged_layout.hh:7-8). Contradicts architecture/how-it-works.md.
  - L70-73 "Verify length and checksum, validate every row, and map the image" -> paged reads check a page on first touch against its parent reference, and a cold start hashes no pack: src/detent_local/trusted_restart.cc:38-123; src/detent_local/paged_read_state.cc:38-110.
  - L94-97 "incremental `paged-v2` publication (the default image format)" -> `paged-v2` and `flat-v1` names and the `image_format` key (src/detent_delivery/configuration.cc:124,141-145) are removed by legacy-i; the page must say there is one format. OWNED legacy-i
  - L55-60 header/rows/regions -> the Dense v3 formats DTPAGE03/DTROOT03 replace DTPAGE02/DTROOT02 (src/detent_local/paged_layout.cc:232,245,262,284). OWNED dense-pages
  - L62 "176-byte `DTSELECT` descriptor ... named `selected`" -> true: include/detent_local/generation.hh:33.
  - L43-44 "123-130 ms to encode and 621-627 ms to decode" -> verified: benchmarks/distribution-codec-comparison.json:82-83 (messagepack, 500k); protobuf :97-98; json :67-68.
  - L28 scope identity "SHA-256 over a fixed domain string, the stable principal, the policy epoch and the sorted list of authorized namespaces" -> true: include/detent/scope.hh:12-39 (domain "detent-authorized-scope-v1", principal, policy_epoch, count, sorted names).
  - L83-86 ledgers "heap, mapped address space, named disk bytes, disk entries and generations" -> true: include/detent_local/disk_ledger.hh:13,17.
  - MISSING: packs and segments, `change`, `storage.meta`, trusted restart, first-touch checks, the paced pack scrub (`scrub_bytes_per_second`), pack compaction (`compaction_interval_ms`): src/detent_delivery/configuration.cc:147-153; include/detent_delivery/coordinator.hh:31,33; docs/mechanisms/trusted-restart.md, change-file.md.
  - MISSING: delivery modes `poll` and `subscription`, `compression` (zstd fragments, level 3), `reset_from`, `image_limits`: src/detent_delivery/configuration.cc:123-130,216-219; src/detent_distribution/framing.cc:7,45.

- `architecture/dependencies.md` | **wrong** | Binary-size table, the `gzip` claim and the library/target lists disagree with the build; the size figures also disagree with benchmarks.md. OWNED legacy-h
  - L76-80 "Default (no CMAKE_BUILD_TYPE) 1.15 MB" -> an unset build type defaults to Release: CMakeLists.txt:9-11. The row describes a build type nobody gets by default.
  - L76-80 sizes 295 KB / 242 KB -> carried only by README.md:261-263 and a comment at CMakeLists.txt:5-7, not by a committed measurement file; benchmarks/logging-footprint.json:59 records the stripped `detent-agent` at 475,520 B and benchmarks.md quotes that. The two pages disagree. Unverified (see UNVERIFIED).
  - L47 "compressors | zstd and gzip encoding and decoding" -> Detent uses zstd only (src/detent_distribution/framing.cc:7,45 `ZSTD_compress` level 3); no gzip in src/ or include/.
  - L7 "Everything is fetched with CMake `FetchContent`" -> OpenSSL 3.0 is a system `find_package` (src/detent_delivery/CMakeLists.txt:4, src/detent_relay/CMakeLists.txt:5), and the page itself lists OpenSSL; the zstd/lz4 source fallbacks are used only when absent (docs/DEPENDENCIES.md:3).
  - L40-54 Libraries -> missing direct dependencies: flat-map (linked by `detent`, src/CMakeLists.txt:29; the read-path page names `detent::flat_map`), completion (src/CMakeLists.txt:63; completion-based storage), hashes (src/detent_local/CMakeLists.txt:24), http-parser for agent and relay (src/CMakeLists.txt:52,99); the manifest holds 35 entries (cmake/dependencies.json), 30 Kronuz.
  - L28-31 Targets table lists two targets -> real targets also include `detent::distribution`, `detent::local_view`, `detent::local_builder`, `detent::local_export`, `detent::local_publication`, `detent::delivery_coordinator`, `detent::authority*`, `detent::relay_*`: src/CMakeLists.txt, src/detent_local/CMakeLists.txt:1-24. `detent` links char_classify, strict_stox, sha256, atomic-shared-ptr, flat_map, random (src/CMakeLists.txt:24-30); `endian` is fetched but not linked by `detent`.
  - L13-26 diagram: `detentd -> detent` -> detentd links `detent_authority_service detent_cli detent_logging http::http reactor::tls logger::logger task::asio` (src/CMakeLists.txt:118-120), `detent` only transitively; `reader "detent"` -> `detent_cli` + `detent::local_client` (src/CMakeLists.txt:106-110) matches.
  - L59-63 "As of November 4 ... pin is its library's default-branch head" -> true as recorded: docs/DEPENDENCIES.md:7; dated text, rewrite as the invariant (a pin never moves unless the manifest changes). CHANGELOG.md:125-134 holds the old/new pins as claimed.
  - L61 "1.1.0 release doesn't compile with Clang 23" -> verified: docs/AUDIT.md:67; cmake/dependencies.json:95 pins 24b5e7a8b27f.
  - L56-58 "CI fails if a pin differs" -> true: .github/workflows/ci.yml:75-83 (`pin-check`), tools/check_pins.py.
  - L5 "most of them extracted from Xapiand" -> Unverified (see UNVERIFIED).

- `architecture/design-documents.md` | **stale** | All 36 links resolve, but two descriptions are out of date and the index omits the directories that now hold the engineering record. OWNED legacy-i, dense-pages
  - L31 SUBSCRIPTIONS_ARCHITECTURE.md "implementation in progress" -> the doc's own status says Stage 5A has component acceptance: docs/SUBSCRIPTIONS_ARCHITECTURE.md:4.
  - L32 LOCAL_IMAGE_FORMAT.md "The generation image and descriptor formats" -> the doc is titled "Local image format, version 1" and specifies the flat image (docs/LOCAL_IMAGE_FORMAT.md:1-7); the paged format lives in INCREMENTAL_PUBLICATION_ARCHITECTURE.md and docs/design/dense-pages.md. OWNED legacy-i, dense-pages
  - L5 "written for contributors" with no pointer to `docs/mechanisms/` (one note per landed mechanism), `docs/design/` (reader.md, namespace-delivery.md, dense-pages.md, observability.md), `docs/perf/`, `docs/measurements/`.
  - MISSING: docs/ACTORS.md (linked from architecture.md), docs/RELAY.md, docs/PRODUCTION_CONTRACT.md, docs/SCALE_TARGETS.md, docs/DEPENDENCIES.md, docs/mechanisms/*, docs/design/*, docs/perf/*, docs/measurements/*.
  - L44 "`benchmarks/*.json`" -> true; the directory also holds the `.cc` sources and CMake targets (benchmarks/CMakeLists.txt).

- `architecture/how-it-works.md` | **stale** | Mixes the store as it is on main with campaign-status narration, a removed-feature history paragraph, designed-not-built sections and a diagram of a component not on main. OWNED dense-pages, reader-s6, reader-s7-s9, namespace-s3b, namespace-s4-s10, legacy-h
  - L86-94 "Legacy wave 2 removed the unmanaged agent. It kept one `detent::Client` ... (formerly `src/detent_agent/agent.hh` ...) The owner ruled to drop it (decision 7 of the wave 2 ledger). Step G deleted it" -> D43 history narration. The one live fact: `detent-agent` and `detent get` exit with `no source configured` (src/detent_agent/main.cc:105,305).
  - L16-29 "What Exists Today" table (state per piece, "Built on branch `reader/s6`", "In progress (step S3b)", "Removed (legacy wave 2, step G)") -> a status ledger, not present-tense description; campaign names (S3b, S7, S9, D6, D8, D9, `Detent:814b605`) are internal jargon on a public page.
  - L49 diagram: "Application\nlibdetent_reader" -> `libdetent_reader` is not on main (no include/detent/reader.h; reads go through `detent::Client` + `make_local_read_source`, include/detent_local/client_source.hh:31). OWNED reader-s6, legacy-h
  - L78-84, L274-307 reader core (open, attach, switch, ring of four, 64 stripe counters) -> designed in docs/design/reader.md, not on main; the page labels it so. OWNED reader-s6, reader-s7-s9
  - L309-316 "Today on `main`: ... polls `selected` every 200 ms" -> true: include/detent_local/client_source.hh:10. Disappears when the reader core lands. OWNED reader-s6, legacy-h
  - L208-247 page layout (80 B header with logical digest, 96 B children, 88 B reference = segment + offset + length + physical + logical SHA-256, Overflow kind, "Two digests per page", Merkle tree) -> true of DTPAGE02 today (include/detent_local/paged_layout.hh:8,9-27; src/detent_local/paged_layout.cc:130-169) but Dense v3 removes the logical-digest fields and the Overflow kind and adds one placement-free state commitment (quest dense-pages A12). OWNED dense-pages
  - L117-136 file table omits `generation-<id>.descriptor`, which each generation also writes: src/detent_local/names.hh:11,19-20; src/detent_local/paged_publication_artifacts.cc:101. Sizes verified: root 320 (paged_layout.hh:8), `.read` 192 (generation.hh:34), `.live` 1152+72/segment+32 (live_record.hh:14), lease/owner 128 and storage.meta 64 (ownership.hh:8-9), `selected` 176 (generation.hh:33).
  - L65 "(`src/detent_authority/change_history.hh:8`)" -> the 4,096 / 16 MiB defaults are on line 9 (line 8 is the struct header); L71 "`include/detent_relay/configuration.hh:8-9`" -> `cache_bytes`/`retained_edges` are on lines 9-10. L83 and L260 citations (client_source.hh:31, segment_mapping.cc:54) verified.
  - L122, L226, L229 citations -> `paged_layout.hh:7` verified; `paged_layout.cc:41-53` and `:27-31` are the validators (27-33, 41-56); `:83-109` points into the key encoders, which start at :78 and end at :112 (decode_key). Citations by line drift; move them to symbol names.
  - L370-374 "`compaction_interval` (60 s by default, `include/detent_delivery/coordinator.hh:31`)" -> the managed.json key is `compaction_interval_ms` (src/detent_delivery/configuration.cc:125,147-150); 0 disables it, otherwise at least 1000.
  - L486-493 scrub: "`scrub_bytes_per_second`: 524,288 by default, 0 disables it" -> true: src/detent_delivery/configuration.cc:152-153; include/detent_delivery/coordinator.hh:33. Relay `/v1/health` fields `scrub_damaged_packs`, `scrub_source_failure` -> true: src/detent_relay/host.cc:112-113.
  - L495-499 "Repair is in progress (step S3b) ... the operator bootstraps a fresh store" -> true today: src/detent_local/trusted_restart.cc:44 ("A missing or wrong record refuses the store"). Page becomes false when repair lands (healing with no operator action). OWNED namespace-s3b
  - L376-390 compacted blocks, three tiers; L394-447 seeding with `detent-image`; L25-27 per-namespace `selected` table and `change` slots -> designed, not on main (no `detent-image` in src/, tools/, CMake; one `change` slot, docs/mechanisms/change-file.md:14-16). The page labels them; they stay pages of future text until built. OWNED namespace-s4-s10
  - L62-64 "answer a producer once a majority has the entry on disk"; L59-61 3, 5 or 7 voters -> true: src/detent_authority/service.cc:373-376; docs/ACTORS.md:59.
  - L344-351 "agent's position inside the upstream's in-memory window: the authority's last 4,096 transactions, or the relay's 64 recent updates" -> true as defaults; both are configurable: src/detent_authority/change_history.hh:9, include/detent_relay/configuration.hh:9-10 (retained_edges 1..256, src/detent_relay/configuration.cc:123).
  - L476-477 "`test_testing_trusted_restart_oracle`" -> a real CTest name: tests/detent_testing/CMakeLists.txt:13-14 (modes oracle, refusals, t5). The "zero pack bytes hashed" assertion itself is Unverified.

- `architecture/read-path.md` | **wrong** | The diagram names a B-tree lookup; the table is a sorted vector (`flat_map`), and the page's own text says so. The "149.1 ns/read" figure is aggregate wall time per op across 4 threads, not per-read latency. OWNED legacy-h
  - L21 d2 "B-tree lookup\n(heterogeneous, string_view key)" -> knob tables are `flat_map` (include/detent/model.hh:55; include/detent/flat_map.hh:6-8), a sorted vector, contradicting L38 and L49-52 of the same page.
  - L83 "6.71M reads/s across 4 threads, 149.1 ns/read" -> benchmarks/detent_read_bench.cc:75-76 prints `elapsed*1e9/total_ops` over all threads (aggregate), so per-thread cost is about 4x that (about 600 ns/op on 4 busy threads); the mix is half rollout evals that build a `std::string` id and hash (benchmarks/detent_read_bench.cc:55-63). "per read" is wrong.
  - L83-84 both figures -> only README.md:352-353 carries them; no file under docs/perf, docs/measurements, docs/status or benchmarks/ does. Unverified.
  - L24 "SnapshotRef::load()", L18 "OverrideTable", L68 `read_view()`/`ClientReadView`, L38 `detent::flat_map` -> exist on main (include/detent/snapshot_ref.hh, overrides.hh, read_view.hh); legacy-h deletes SnapshotRef, OverrideTable, the read views and the first evaluator. Rewrite when it lands. OWNED legacy-h
  - L5-8 "no network call, file read, mutex, sleep, retry, logging I/O or heap allocation" -> counters are relaxed atomics (src/detent/client.cc:176-198); name check and override lookup precede the load (client.cc:170-188). Holds for the in-memory path; the local-source path goes straight to `read_source_->read_view()` first (client.cc:165-166, 205-206).
  - L86-87 "clean under ASan, UBSan and TSan on Linux" -> CI runs Clang ASan/UBSan and TSan jobs (.github/workflows/ci.yml:22-27); the platform of those jobs is the shared workflow's. tests/detent/test_concurrency_stress.cc exists. Treat as Unverified for "on Linux".
  - MISSING: the managed-generation read path as it runs today (first-touch bitmap, trusted pages; docs/mechanisms/verified-page-bitmap.md) and read counters (`DETENT_READ_COUNTERS`, CMakeLists.txt:43-47).

- `architecture/relay.md` | **missing content** | Every checked claim holds; the page omits the config keys, limits, flags and health fields. OWNED namespace-s4-s10, observability
  - L40-46 "relay sources always subscribe: an explicit `\"delivery_mode\": \"poll\"` is rejected" -> true: src/detent_relay/configuration.cc:117-120.
  - L43 "TLS on `127.0.0.1:8318` by default" -> true: include/detent_relay/configuration.hh:15; src/detent_relay/configuration.cc:90.
  - L43-44 "serves only POST /v2/distribution/session/open, /next and /close, plus GET /v1/health" -> true: src/detent_distribution_http/session_request.cc:84-88; src/detent_relay/service.cc:359,374,405.
  - L49-50 "64 recent edges and 64 MiB per bundle" -> true: include/detent_relay/configuration.hh:9-10.
  - L55-56 `"mode": "fresh"` / `"existing"` -> true: src/detent_relay/configuration.cc:107-113.
  - L65 "`-DDETENT_BUILD_RELAY=ON` (off by default; requires OpenSSL 3.0)" -> true: CMakeLists.txt option; src/detent_relay/CMakeLists.txt:5.
  - L66-67 "`relay.json` in the user configuration directory, or the system one with `--system`" -> true: src/detent_relay/main.cc:43.
  - L67-69 bundle directories `0700`/`0600` -> files 0600 true (src/detent_relay/configuration.cc:121); `/var/lib/detent-relay` (systemd StateDirectory, packaging/systemd/detent-relay.service:13) and `/var/db/detent-relay` (packaging/rc.d/detent_relay:14) verified; the macOS path is recommended in docs/RELAY.md:114 but no launchd template creates it. Unverified for macOS.
  - L57-58 "health reports ready only after both" -> true: src/detent_relay/host.cc:327-333 (ready only when every bundle is ready).
  - MISSING: relay.json keys `address, port, hostname, certificate, private_key, ca, bundles, credentials, control_bytes, baseline_bytes, heap_bytes, connections, service_limits`; per-bundle `source, directory, mode, cache_bytes (>= 1 MiB), retained_edges (1..256)`; at most 256 bundles; credential keys `token, principal, policy_epoch, bundles`: src/detent_relay/configuration.cc:59-62,99-109,143.
  - MISSING: `detent-relay --config --system --daemonize --status --runtime-dir` (src/detent_relay/main.cc:17-39) and the health JSON (`ready`, per-bundle `revision, ready, authenticated, fenced, gaps, refusals, scrub_*, sync_*`): src/detent_relay/host.cc:73-130. Per-namespace sessions and usage summaries upstream come with namespace-s4-s10 and observability.

- `architecture/update-path.md` | **wrong** | The diagram and the backoff section claim behavior the updater does not have; it also documents a source the repo does not contain. OWNED legacy-h
  - L15 d2 "poll source\n(file metadata or HTTP)" -> no HTTP source exists: src/detent/client.cc has a file source (:293-334) and a generic `source_poll` function seam (:355-399); nothing in src/ supplies an HTTP poll.
  - L25 d2 "parse -> backoff: invalid" and L67 "Transport failures and invalid updates back off with full jitter" -> only a thrown source error backs off (src/detent/client.cc:382-397: 250 ms x 2^n, cap 30 s, jittered). A rejected or stale candidate only bumps `refresh_failures`/`stale_updates` and waits the plain poll interval (client.cc:403-413, 434-439); the file source never backs off at all (client.cc:293-334, `sleep_with_stop_check(poll_interval)` at :333).
  - L40-48 `live_snapshot_`, `current_` and patch application onto the client's own last accepted state -> true: src/detent/client.cc:443-455; include/detent/client.hh:226-227.
  - L76 "`InitOptions::on_update` ... must be fast and must not throw" -> true: include/detent/client.hh:110-115; client.cc:426.
  - L69-70 "Staleness is measured on `steady_clock`" -> true: src/detent/client.cc:22-25.
  - L85 "applying a one-knob patch to 500,000 knobs takes about 780 ms" -> verified: benchmarks/scaling-baseline.json:28. L84 "35 us for 100 knobs" -> only README.md:354; Unverified.
  - L37 "A source returns either a full snapshot or a patch (`is_patch`)" -> true: include/detent/client.hh:78,91.
  - MISSING: the file source's change gate (reads only when mtime or size changed; a malformed file is not re-parsed until it changes): src/detent/client.cc:310-331; `poll_interval` default 200 ms, `refresh_alert_after` 5 min, `max_knobs` 100,000: include/detent/client.hh:100-106.
  - Whole page describes `detent::Client`, `InitOptions`, file and poll sources: all deleted by legacy-h (the Reader refresh model replaces them). OWNED legacy-h

- `about.md` | **current** | Marketing summary; every checked claim holds on main, several name the Client and snapshot-file source that legacy-h removes. OWNED legacy-h, reader-s6, reader-s7-s9
  - L26 "Live updates from a snapshot file, or managed delivery from the authority" -> file source exists on main (src/detent/client.cc:130-141); removed by legacy-h. OWNED legacy-h
  - L36 "C++20 library, Python and Rust bindings, and a shell reader" -> true on main (bindings/python, bindings/rust, `detent get`); the library becomes `libdetent_reader` with a C ABI and the bindings sit on it. OWNED legacy-h, reader-s6, reader-s7-s9
  - L37 "Linux, macOS and FreeBSD, with systemd, launchd and rc.d templates" -> templates verified (packaging/systemd, launchd, rc.d); FreeBSD is not in CI (`freebsd: false`, .github/workflows/ci.yml:25). Unverified for FreeBSD.
  - L30 "compare-and-set" -> true: src/detent_authority/state.hh:35 (`expected` revision, 0 means absent).
  - MISSING: the relay, the Python/Rust/shell reading paths by name, Homebrew packaging (packaging/homebrew/detent.rb).

- `benchmarks.md` | **wrong** | The 150 ns projection misreads aggregate ns/op, the footprint table measures an agent form that no longer exists, a planning target is misquoted, and one column cannot be reproduced with the listed command. OWNED legacy-i, legacy-h, observability
  - L26-29 "At ~150 ns per read, a service doing 100,000 knob reads per second spends about 0.015 of one core" -> 149.1 ns/op is `elapsed*1e9/total_ops` over 4 threads (benchmarks/detent_read_bench.cc:75-76), so it is aggregate wall per op; per-thread cost is about 4x (about 600 ns), and half of the benchmark ops are rollout evals (:55-63). The projection is understated by about the thread count.
  - L82-85 Footprint "detent-agent 475,520 bytes ... 1,316 KiB RSS, 2 threads" -> benchmarks/logging-footprint.json:59-70 measured "an agent with one small defaults file and no source" (docs/LOGGING.md:46). That unmanaged form is gone: `detent-agent` without a managed source exits `no source configured` (src/detent_agent/main.cc:105). The managed agent's idle RSS and size are not measured by any committed file. Also contradicts architecture/dependencies.md L78-80 (242 KB stripped).
  - L97-98 "the planning target of 50,000 agents is a test target" -> docs/SCALE_TARGETS.md:15 lists 250,000+ agents as the target and 50,000 as the previous planning number.
  - L49 "Flat image encode" column (19.778 ms / 108.445 ms) -> carried by benchmarks/scaling-baseline.json:16,29, but `detent_scale_bench` prints no image time (benchmarks/detent_scale_bench.cc:64-65 prints json_bytes, serialize, parse, patch only) and no committed bench prints one: not reproducible with `build/benchmarks/detent_scale_bench 100000 5`. "Flat" names the format legacy-i removes. OWNED legacy-i
  - L44-52 corpus and rows -> verified: benchmarks/scaling-baseline.json:5-16 (100,000; 5 iterations; json_bytes 11123377; parse 296.13; patch 148.624), :21-29 (500,000; 3 iterations; 56283377; 1708.244; 780.557); Apple Clang 17, x86_64, Release: :2-4.
  - L18-19, L38 read/hash/update rows (6.71M, 149.1; 5.05M, 198.2; 28.6K, 35.0) -> only README.md:352-354 carries them; the page's opening claim that every number comes from `benchmarks/` or a recorded artifact does not hold for them. Unverified.
  - L58-62 authority tables (133.708 / 0.0127 / 26; 668.599 / 0.0113 / 27) and L63-66 (490 tx/s, 0.042 cores) -> carried only by docs/AUTHORITY_ARCHITECTURE.md:112-113,138 and docs/SCALE_TARGETS.md:106 (which adds "one voter, local disk, no HTTP, TLS or replication"); no committed artifact under benchmarks/ holds them. Unverified.
  - L69-73 commands -> targets exist: benchmarks/CMakeLists.txt:21-29 (`detent_scale_bench`, `detent_authority_bench`), :40-43 (`detent_authority_durable_bench`, needs DETENT_BUILD_AUTHORITY); argument shapes match benchmarks/detent_scale_bench.cc:2, detent_authority_bench.cc:2,31-33, detent_authority_durable_bench.cc:2. `build/benchmarks/` is the output dir; the L18-19, L38 commands omit it.
  - L21 "one 14-core arm64 development machine" -> README.md:348 only; Unverified. L19 `stable_bucket()` bench loop includes a `std::to_string` per call (benchmarks/detent_hash_bench.cc:26-27).
  - MISSING: measurements on main that exist: 5M-record cold bootstrap and restart (docs/perf/cold-bootstrap-5m.md, sync-groups.md, trusted-restart.md, recovery-digest-once.md), flat_map vs std::map (docs/perf/flat-map.md), reader and Dense measurements under docs/measurements/, and the other benchmark targets in benchmarks/CMakeLists.txt (`detent_distribution_codec_bench`, `detent_local_image_bench`, `detent_paged_*_bench`, `detent_fleet_load`).
  - MISSING: how to build the benchmarks (`DETENT_BUILD_BENCHMARKS` ON by default, CMakeLists.txt option; CI turns it off, .github/workflows/ci.yml:19,36-38).
  - L90-101 "What Hasn't Been Measured" -> items 1-3 hold; fleet qualification artifacts exist (benchmarks/fleet-bootstrap-qualification.json, fleet-persistent-transport-qualification.json) and should be named.

### 2. Features and quick start

- `index.mdx` | **current** | every checked claim is true on main; the C++ client and bindings are named as shipped surface [OWNED legacy-h, reader-s7-s9]
  - L62 "Link the C++ client, use the Python or Rust bindings" -> true today: `detent::Client` at include/detent/client.hh:121 (`class Client`), bindings/python/detent_python.cc:28-43, bindings/rust/src/lib.rs:75-130. Text must be rewritten when legacy-h lands (Client deleted, `Reader` replaces it; bindings become thin layers in reader-s7-s9).
  - L21 "no network, no disk, no lock, no allocation" -> true on the `Client` path (src/detent/client.cc:164-199: one `current_.load()`, one find, errors allocate only on failure). On the managed mapped path the guarantee is "warm lookup" (docs/design/reader.md:124 D3); first touch of a page verifies it (docs/design/reader.md:43). Add the "warm" qualifier or keep it Client-only.
  - MISSING: single-voter mode (the quick-start's own topology) vs the "three voters" card: src/detent_authority/service.cc:416-418 (no `voters` = single voter), :373 (`voters` needs >= 3, odd).
  - MISSING: relay (`detent-relay`, built only with `-DDETENT_BUILD_RELAY=ON`, CMakeLists.txt:43) appears on no feature card.

- `welcome.md` | **wrong** | names an HTTP source that does not exist on main; "every identifier is `detent`" is false; the copy-as-curl note points at plain HTTP while the tutorial authority serves HTTPS [OWNED legacy-h, reader-s6]
  - L14 "keeps that snapshot fresh from a file, an HTTP endpoint, or a durable authority" -> `InitOptions` offers `source_path` (file), `source_poll` (a function) and `read_source` (local generations) only: include/detent/client.hh:62,99,118. No HTTP source, no `/v1/snapshot` route: docs/AUTHORITY_ARCHITECTURE.md:80, website guides/durable-authority.md:15. The durable authority reaches readers through `detent-agent`, not the Client's updater thread.
  - L23 "`detent::Client`, the library your application links. It owns the read path, the evaluation rules and the background updater" -> true on main (include/detent/client.hh, src/detent/client.cc). OWNED legacy-h: Client is deleted there; rewrite around `Reader`/`libdetent_reader`.
  - L38 "Every identifier in the code is `detent`" -> history narration about another product; removed with D66.
  - L58-60 "equivalent curl command, aimed at a local detentd on its default port 8317" -> astro.config.mjs:76 emits `http://localhost:8317`; the quick-start authority sets `publisher_tls` (quickstart.md:68), so it serves HTTPS (src/detentd/main.cc:166-170, 192-193) and the generated curl fails. `GET /v1/health` itself exists: src/detent_authority/service.cc:716.
  - L21-31 "three moving parts" -> omits the optional relay (`detent-relay`), which README.md:24 counts as an actor.
  - L33-41 "Detent began as a faithful, independent clone..." -> history narration (D43 prefers present tense); keep only a link to `docs/design/knob-model.md` if wanted.

- `basics.md` | **wrong** | the Source section names a nonexistent HTTP source; "new knob names are introduced" in baked defaults contradicts the merge and the quick-start [OWNED legacy-h, reader-s7-s9, dense-pages]
  - L77-78 "an HTTP endpoint that serves `GET /v1/snapshot`" -> no such endpoint or source on main (include/detent/client.hh:62,99,118; docs/AUTHORITY_ARCHITECTURE.md:80). Sources are: none, `source_path`, `source_poll`, `read_source` (local generation).
  - L63-64 "They are also where new knob names are introduced" -> a live-only knob is accepted and served: `merge_over_defaults` keeps entries from both tables (src/detent/json_schema.cc:380-382); the managed path tries the live image first (src/detent_local/client_source.cc:44-49). The quick-start publishes `search:max_results` with no defaults file.
  - L54-57 "Every published snapshot carries a positive integer `version`... counted as stale" -> true of the file/poll source (src/detent/json_schema.cc:285-290, src/detent/client.cc:412-415). The managed path orders by a cursor `revision` (src/detent_agent/main.cc:139) that `detent get` and the quick-start replies show (`revision`, `namespace_revision`); the page never defines it.
  - L59-65 "Baked defaults ... `defaults.json`" -> true: src/detent_agent/config.cc:70-83 (`defaults.json` beside agent.json). OWNED legacy-h / reader-s7-s9: `detent get` loses `--defaults` until S7 (D21).
  - L17-27 name rules -> true: include/detent/validation.hh:3,22-33 (one colon, 200 bytes, `A-Za-z0-9._/-`).
  - L35-39 definition shape -> true: src/detent/json_schema.cc:62-130 (`type`, `default`, `rollout_pct`, `overrides`; unknown field rejected).
  - MISSING: error categories a reader sees beyond MissingKnob: Uninitialized, InvalidName, WrongType, ConversionFailed, SourceFailure: include/detent/errors.hh:9-16.
  - MISSING: `rollout_pct: null` is accepted and clears the gate: src/detent/json_schema.cc:115-117.

- `installation.md` | **wrong** | the Homebrew formula pins a pre-authority revision, and the "Apple Clang 17 works" claim conflicts with the formula's own libc++ note; option table omits two options [OWNED rc5, legacy-h]
  - L13-18 "`brew install Kronuz/tap/detent` installs `detentd`, `detent-agent` and `detent`" -> the formula pins `323b971` (2026-10-09): packaging/homebrew/detent.rb:11-16. At that revision `detentd` is the old distributor (`git show 323b971:src/detentd/main.cc:1-12`: `detentd <port> <persist_path>`, no `--authority`) and `detent-agent` reads `--source-path`/`--source-address` (`git show 323b971:src/detent_agent/main.cc:106-124`). The quick-start commands do not run against it. D22 keeps the tap at that pin until the owner moves it; rc5 A8 covers the release.
  - L24-26 "libc++ 20 and newer, recent libstdc++, and Apple Clang 17 all work" -> packaging/homebrew/detent.rb:36-39 adds `-fexperimental-library` on macOS before 26 because the SDK's libc++ 19 hides `std::jthread`; CMakeLists.txt:38 fails configure without it. CI builds macOS with Homebrew LLVM (`macos_llvm: true`, .github/workflows/ci.yml:21), not Apple's libc++. Unverified (see UNVERIFIED).
  - L34 "developed and qualified on macOS, Linux and FreeBSD" -> CI skips FreeBSD (.github/workflows/ci.yml:25 `freebsd: false`); FreeBSD native runs are recorded in docs/AUTHORITY_READINESS.md:39.
  - L80-88 option table -> defaults ON/OFF match CMakeLists.txt:41-50. MISSING: `DETENT_BUILD_RELAY` (OFF; builds `detent-relay`, CMakeLists.txt:43, src/CMakeLists.txt:94-100) and `DETENT_READ_COUNTERS` (OFF, CMakeLists.txt:50).
  - L51 `-DCMAKE_BUILD_TYPE=Release` -> redundant: an unset type already becomes Release (CMakeLists.txt:9-11).
  - L55-61 binaries table -> true for `detentd`, `detent-agent`, `detent` (src/CMakeLists.txt:102,111,121). MISSING: `detent-relay` with the relay option.
  - L90-95 library-only build with both options OFF -> flags exist (CMakeLists.txt:44-45); that it configures and builds is Unverified.
  - L28-30 "every dependency is fetched with `FetchContent`" -> OpenSSL is found on the system (src/detent_delivery/CMakeLists.txt:4); LZ4/Zstandard are system libraries with pinned source fallbacks (docs/DEPENDENCIES.md:5).
  - L117-119 install layout -> true: src/CMakeLists.txt:102-104,111,121 (also installs docs/SERVICES.md and, with the relay option, `detent-relay`).
  - L36-42 FreeBSD hint, L141-142 "no tagged releases" -> true: CMakeLists.txt:38; `git tag` is empty.

- `quickstart.md` | **current** | every command, flag, config key, port and path matches the code; two outputs rest on timing and are listed Unverified [OWNED daemon-ready-leader]
  - L104-107 "`--daemonize` returns once the authority listens, and the single voter elects itself leader shortly after... the loop retries" -> true today: ready() fires after `service.start` (src/detentd/main.cc:190-191). daemon-ready-leader (D23) makes the daemon wait for a leader; the retry loop, the `outcome_unknown` transcript and L101 then go.
  - Checked true: flags `--authority --runtime-dir --daemonize` (src/detentd/main.cc:117-126), `--managed --generation-dir --runtime-dir --initialize-managed --daemonize` (src/detent_agent/main.cc:394-405); authority keys `cluster group epoch directory publisher_tls credentials{token,producer,principal,publish,namespaces}` (src/detent_authority/service.cc:207-209,432), `directory`/cert paths relative to the config file (:354-365); managed keys `cluster group epoch principal namespaces token ca` (src/detent_delivery/configuration.cc:101-130,155-172,201-202), `ca` relative to the config; port 8317, address 127.0.0.1, hostname `localhost` (:176-199); single voter without `voters` (service.cc:416-418); routes and fields `/v2/open {expected_generation,nonce}`, `/v2/transactions {generation,sequence,namespace,operations[{key,value}]}` (service.cc:775-811); `null` deletes (service.cc:804-806); 503 `outcome_unknown` (service.cc:169,1003); `DETENT_GENERATION_DIR` (src/detent_agent/main.cc:410); pid files `run/agent.pid`, `run/detentd-8317.pid` (main.cc:68; src/detentd/main.cc:138); fixture writes `ca.pem`, `one.pem/.key`, `-days 1`, SAN `localhost` (tests/detent_authority/tls_certificates.cmake:13-21); rollout buckets user-1=10, user-2=93, user-42=2 recomputed with SHA-256.
  - MISSING: identities must be lowercase hex and nonzero (src/detent_authority/service.cc:101-117); only `rand -hex` output is safe.
  - MISSING: authority config file must be a regular file owned by the user, not group/world writable (service.cc:192-196); managed.json is checked the same way (src/detent_cli/config.cc:156-161 pattern, `chmod 600` on the page covers it, the reason is not stated).
  - MISSING: per-transaction limits: 128 operations, 256 KiB request (src/detent_authority/state.hh:27-29); a published definition over 16 KiB or with over 256 overrides is refused with 400 `invalid_request` (service.cc:1110-1117, state.hh:29).

- `settings-and-flags.md` | **missing content** | all checked claims are true; `--default` and the shell reader are described too loosely [OWNED legacy-h, reader-s7-s9]
  - L101-102 "`--default` supplies a value for one read when the knob can't be read at all" -> it applies to every read error except SourceFailure (src/detent_agent/main.cc:294-296), so a damaged pack still errors. Without it the read prints `ERR ...` to stderr and exits 1 (main.cc:298-300).
  - L30-38 C++ `get`, `get_as<T>`, `get_or_panic`, `eval_or_panic` -> true on main (include/detent/client.hh:136-178). OWNED legacy-h: rewrite for `Reader`.
  - L93-95 "A client whose baked defaults contain `search:timeout_ms` falls back to that baked value" -> true for Client and the local read source (src/detent_local/client_source.cc:44-60). OWNED legacy-h/reader-s7-s9: `detent get` has no defaults until S7 (D21), `--defaults` is read at src/detent_agent/main.cc:235.
  - L14 "Int knobs hold any signed 64-bit integer" -> true: src/detent/json_schema.cc:154 (`IsInt64`).
  - MISSING: `detent get` prints the type it finds (`true`/`false` or decimal) by trying bool then int (src/detent_agent/main.cc:179-194).
  - MISSING: `--hash`, `--switch`, `--default` are three of the reader's flags; also `--config --defaults --runtime-dir --managed --generation-dir --system --publisher-user --reader-validation` and `--generation-cluster/-group/-epoch/-scope`: src/detent_agent/main.cc:154-167,312-323.
  - MISSING: shell `detent get` auto-starts a user agent when `--managed` is set and no generation exists (main.cc:267-281).

- `rollouts.md` | **current** | formula, bucket math, edge cases and example values verified [OWNED legacy-h]
  - L43-49 `bucket = u64_be(SHA256(knob || 0x00 || hash)[0..8]) % 100`, `bucket < rollout_pct` -> true: src/detent/hashing.cc:7-20; include/detent/detail/evaluation.hh:20-21; src/detent_local/evaluation.hh:17-28 (see docs/mechanisms/evaluation.md).
  - L60 "`0` enables nobody, `100` enables everybody who supplies a hash value" -> true (bucket in 0..99, exclusive compare).
  - L84-88 no hash -> default -> true: include/detent/detail/evaluation.hh:20-21; src/detent_local/evaluation.hh:17-19.
  - L26-29, L74-82 example outputs -> user-1 bucket 10, user-42 bucket 2: false/true at 10%, true at 50% and 100%.
  - L31-33 C++ `client.eval_or_panic(name, user_id)` -> true on main (include/detent/client.hh:171-174). OWNED legacy-h.
  - MISSING: `rollout_pct` must be an integer 0..100 or null; anything else is refused (400 `invalid_request`): src/detent/json_schema.cc:115-126; src/detent_authority/service.cc:807, 1110-1117.
  - MISSING: no page shows the refusal path (a wrong-typed definition); the quick-start never publishes an invalid knob.

- `group-overrides.md` | **missing content** | semantics and outputs verified; limits and validation absent [OWNED legacy-h]
  - L48-56 evaluation order -> true: include/detent/detail/evaluation.hh:9-22 and src/detent/client.cc:171-180 (CLI override first); group rollout with no hash returns default (evaluation.hh:16-18). Shell `detent get` has no CLI-override option, so step 1 applies to the C++/binding client only.
  - L72-73 "Group names are opaque UTF-8 strings of 1 to 128 bytes" -> true: include/detent/validation.hh:35-37; src/detent/json_schema.cc:95-96.
  - L65-69 same bucket across group and knob-wide -> true (evaluation.hh:11-21 uses one `stable_bucket(name, hash)`).
  - L30-37, L85-97 example outputs -> user-1 bucket 10 (beta 50: true; production 5: false; unknown group 10%: false); user-2 bucket 93 (beta 50: false, beta 100: true).
  - L104-105 int knobs ignore the switch -> true: src/detent/client.cc:204.
  - L40-42 C++ `eval_or_panic(name, user_id, user_group)` -> true on main (include/detent/client.hh:171-174). OWNED legacy-h.
  - MISSING: at most 256 overrides per definition and 16 KiB per definition: src/detent_authority/state.hh:29; publishing more returns 400 `invalid_request` (service.cc:1110-1117).
  - MISSING: an override value is `true`, `false` or an object with exactly one member `rollout_pct` (0..100); a duplicate group key is refused: src/detent/json_schema.cc:101-114, 142.

- `README.md` | **wrong** | several statements contradict main (suite count, new-knob-names rule, PATCH, "Unset" build size row, dependency graph, source shapes) and the image-format paragraph describes a mechanism the owner removes [OWNED legacy-h, legacy-i, dense-pages, reader-s6, reader-s7-s9, rc5, observability]
  - L48 "they are the place where new knob names are introduced" -> live-only knobs are accepted: src/detent/json_schema.cc:380-382; the quick-start publishes with no defaults file.
  - L90-95 "One `Client`, three source shapes: None, Local file, Custom poll" -> main has a fourth, `read_source` for managed local generations (include/detent/client.hh:118; src/detent/client.cc:112,126; usage in docs/USAGE.md:64). OWNED legacy-h.
  - L62-85 `detent::InitOptions`, `detent::Client`, `defaults_path`, `cli_overrides` and L87 `detent::global::init()` -> exist today (include/detent/client.hh:59-118, include/detent/global.hh:16); legacy-h deletes them. Section 3 and 4 and 6 must be rewritten around `Reader` when it lands. L120-128 `TestKnobs` likewise (include/detent/testing.hh:15-23).
  - L11 "Integer knobs do not support per-group overrides in v1" -> true (src/detent/client.cc:204); "in v1" is version text (D43): drop it.
  - L139 "The wire format remains `schema_version: 1`. Full snapshots, PATCH payloads, and diff responses all validate through the same schema" -> `schema_version: 1` is the defaults/file grammar (src/detent/json_schema.cc:262-286). Authority writes are `/v2/transactions` and delivery is the binary protocol (include/detent_distribution/protocol.hh); detentd has no PATCH or `/v1/snapshot` (docs/AUTHORITY_ARCHITECTURE.md:80). Stale.
  - L387 "PATCH replaces a complete knob definition, including all its groups" -> a transaction operation does (src/detent_authority/service.cc:795-811); PATCH is not an authority route. Stale.
  - L141-154 dependency graph -> `detent` also links `flat_map::flat_map` (src/CMakeLists.txt:29); missing from graph and from the L162-173 table. The table lists 10 of the manifest's 35 entries (cmake/dependencies.json; docs/DEPENDENCIES.md:7): no `flat-map`, `task`, `completion`, `journal`, `metrics`, `logger`, `stash`, `lz4`, `zstd` and others. L160 "All dependencies are fetched with FetchContent" -> OpenSSL is a system dependency (src/detent_delivery/CMakeLists.txt:4).
  - L175 "the older 1.1.0 release contains a `GenericStringRef` assignment operator that fails to compile with Clang 23" -> pin is true (cmake/dependencies.json:95); the "older 1.1.0" clause is history narration and its compile claim is Unverified.
  - L177-206 layout -> `include/detent_testing/` and `src/detent_testing/` exist and are unlisted (also `cmake/`, `packaging/`, `tools/`, `website/`).
  - L212 "Managed agents and relay bundle sources write `\"image_format\": \"paged-v2\"` by default; `\"flat-v1\"` selects one self-contained image. Both formats remain readable... existing flat heads migrate before paged updates" -> true on main (src/detent_delivery/configuration.cc:141-146; include/detent_delivery/coordinator.hh:30). OWNED legacy-i (removes `image_format`, `flat-v1`, `paged-v2`) and dense-pages (one format, refuse and refetch): the sentence and the L212 anchor to docs/LOCAL_IMAGE_FORMAT.md:237 go. Also compatibility narration (D43).
  - L229-236 binaries table -> `detent-relay` is built only with `-DDETENT_BUILD_RELAY=ON` (default OFF, CMakeLists.txt:43; src/CMakeLists.txt:94-100); the page never says so. L253 lists AUTHORITY/PYTHON/RUST defaults only: MISSING `DETENT_BUILD_RELAY` and `DETENT_READ_COUNTERS` (CMakeLists.txt:43,50).
  - L255-263 binary-size table: "Unset `CMAKE_BUILD_TYPE` 1.15 MB" -> an unset type now builds Release (CMakeLists.txt:9-11); the sizes were of the removed unmanaged agent (git 4f49f4b) and are in no committed measurement file. Unverified.
  - L368 "8 automated suites" for the client -> tests/detent/CMakeLists.txt:1-16 registers 14 (12 under TSan).
  - L370 "ASan/UBSan and TSan run clean across the full suite" -> CI configures both (.github/workflows/ci.yml:21-27); a clean run is Unverified (needs the latest main run).
  - L371-372 "Real network conditions: Not done... Soak: Not done" -> docs/measurements/reader-s6-soak-20261107/README.md records a concurrent soak and sanitizer gate; revisit the table against docs/measurements and docs/status.
  - L352-354 benchmark numbers -> not in any committed measurement file; docs/perf/m1-w5-evidence.md:75-78 reports `detent_read_bench` at 260-431 ns/op on an Intel i9 host. Unverified. OWNED rc5/legacy-h: docs/design/client-local-source.md:127 retires `detent_read_bench`.
  - L391 "the measured 100,000/500,000-knob baseline" -> docs/SCALING.md is a 2026-10-02 preliminary plan (docs/SCALING.md:5) whose L31 describes the removed distributor; the committed baseline is benchmarks/scaling-baseline.json.
  - L285-293 agent config text -> true: src/detent_agent/config.cc:48-61,70-83; src/detent_cli/config.cc:20-48; flags > env > config (src/detent_agent/main.cc:82-87). MISSING: env `DETENT_DEFAULTS_PATH` (main.cc:235), `DETENT_MANAGED_CONFIG`, `DETENT_GENERATION_DIR` (main.cc:409-410), `--upgrade-managed` (main.cc:102,116), `--reader-validation`, `--publisher-user`, `reader_*` capacity keys (src/detent_agent/config.cc:56,62-67).
  - L273 detentd paragraph -> true: src/detentd/main.cc:37-54,91-108,131-133; default port src/detent_cli/config.hh:10. MISSING: `--system`, `--reactors` default 4 (main.cc:133), `DETENTD_PORT/REACTORS/RUNTIME_DIR` envs (main.cc:102-108).
  - L280-283 `detent get ... --managed`, `detent-agent --status --managed`, the awk one-liner -> true: src/detent_agent/main.cc:125-146,230-307; `running (pid N)` output at :128.
  - L22-28 actors, L24 relay, L28 links -> links resolve; anchors `#writer-preference-and-maintenance` (docs/LOCAL_IMAGE_FORMAT.md:237), `#managed-authority-delivery` (docs/USAGE.md:196), `#persistent-agent-configuration` (docs/USAGE.md:144) exist.
  - L304-335 bindings and examples -> true on main (bindings/python/detent_python.cc:28-90; bindings/rust/src/lib.rs:81-130). OWNED legacy-h, reader-s7-s9: pybind11 and cxx are replaced.
  - L106-108 and L9-14: authority/consistency/security claims -> true: bearer tokens (src/detent_authority/service.cc:432-446), mutual TLS with pins (:371-400). L9 "does not touch the network, disk, or a mutex" is the warm `Client` path.
  - MISSING: no mention of `detent-agent --status` growing `--usage`, the usage page or the Prometheus textfile (design only: docs/design/observability.md). OWNED observability when it lands.

- `QUICKSTART.md` | **current** | same commands as quickstart.md, all verified against the code; the final status output is reproduced from the recipe's revision count [OWNED daemon-ready-leader, observability]
  - L71 "`--daemonize` returns once the authority listens... Until then the open returns `outcome_unknown`, and the loop retries" -> true today (src/detentd/main.cc:190-191); daemon-ready-leader removes the retry and the transcript at L73-79.
  - L227-230 `detent-agent --status ...` prints `running (pid N)` then `durable revision=8 knob_count=2` -> format true (src/detent_agent/main.cc:128,139); `revision=8` and `knob_count=2` follow from 8 transactions (open is revision 0) and two surviving knobs. OWNED observability: `--status --usage` is added (D47 Q3).
  - L238-241 links resolve (docs/USAGE.md, docs/AUTHORITY_ARCHITECTURE.md, docs/SERVICES.md, README.md).
  - L204-209 `--default false` -> true (src/detent_agent/main.cc:294-296); only SourceFailure bypasses it (see settings-and-flags).
  - MISSING: same gaps as quickstart.md (lowercase hex, limits, readiness).

### 3. Usage guides and operations

- `guides.md` | **wrong** | the index promises an HTTP-endpoint source that does not exist and omits the Durable Relay guide; Client wording OWNED legacy-h
  - L16 "feed clients from a local file or an HTTP endpoint" -> only a file source (`source_path`) and a caller function (`source_poll`) exist; no HTTP source: include/detent/client.hh:62,99; src/detent/client.cc:109-136. guides/live-updates.md says "file or poll function".
  - L14 "construct a client" / L12-18 section "Reading Knobs" -> `detent::Client` is described as the read API; OWNED legacy-h (Client removed, `Reader` replaces it): rewrite when it lands.
  - MISSING: no entry for guides/durable-relay.md (page exists; the "Running the Servers" list names only Durable Authority and Managed Delivery): guides.md:29-33.
  - MISSING: no entry for the operations pages or the reference for the `detent`/`detent-agent` CLIs (only guides are indexed).

- `guides/cpp-application.md` | **missing content** | true on main today but describes `detent::Client`/`InitOptions`/`detent::global`; consumer build and optional defaults omitted [OWNED legacy-h]
  - L1-201 whole page: `Client` (include/detent/client.hh:121), `InitOptions` (client.hh:59), `detent::global` (include/detent/global.hh:17-45) exist on main; legacy-h deletes them: page must be rewritten around `Reader` when it lands.
  - L22 "`detent::detent` has no networking dependencies" -> true of the target (src/CMakeLists.txt:1-34) but the L11-17 `FetchContent_MakeAvailable(detent)` configures the whole root project: tests, examples, benchmarks and the authority (OpenSSL 3, Asio, consensus) are ON by default: CMakeLists.txt:41-48,95; src/CMakeLists.txt:95.
  - MISSING: the consumer CMake needs `set(DETENT_BUILD_AUTHORITY OFF)`, `DETENT_BUILD_MANAGED_AGENT OFF`, `DETENT_BUILD_TESTS/EXAMPLES/BENCHMARKS OFF` before `FetchContent_MakeAvailable` to get only the library: CMakeLists.txt:41-48,98-112.
  - L61 "throws `std::runtime_error` if the file is missing or invalid" -> true only when `defaults_path` is set; an empty `defaults_path` is valid and starts with no knobs: include/detent/client.hh:60; src/detent_agent/main.cc:4-5. MISSING: defaults file is optional.
  - MISSING: `InitOptions::max_knobs` (100,000), `refresh_alert_after` (5 min), `on_update`, `read_source`, `Metrics` (`client.metrics()`): client.hh:101-118,196.
  - L43 "A defaults file has no `version`" -> true, and a `version` or `published_at` in a defaults file is rejected: src/detent/json_schema.cc:285-298.

- `guides/durable-authority.md` | **missing content** | request/response shapes verified; the HTTP surface, limits and status codes the code serves are not on the page [OWNED daemon-ready-leader]
  - L116-121 table "Outcome ... `503` `outcome_unknown`" -> true; wire statuses also include `invalid`, `unknown_session`, `session_fenced`, `out_of_order`, `result_unavailable`, `identity_conflict`: src/detent_authority/service.cc:152-155. HTTP codes: 200 applied/no_change, 429 capacity, 409 the rest: service.cc:171-181.
  - MISSING: `POST /v2/close` (body `generation`): service.cc:781-785. `GET /v1/health` (unauthenticated, no body allowed): service.cc:716-722,1066.
  - MISSING: one in-flight transaction per producer, else `429 {"error":"producer_busy"}`: service.cc:855-858. Request body cap 256 KiB, 128 operations per transaction, 128 keys per read, 500,000 keys, 4,096 namespaces, 1,024 producers (all tunable via `application_limits`): src/detent_authority/state.hh:25-29; service.cc:207-233.
  - MISSING: `400 invalid_request` with `detail` for malformed or foreign-identity bodies; `404 unknown_endpoint`; keys must carry the transaction's namespace prefix: service.cc:833,1114-1116; src/detent_authority/state.cc:212.
  - L60 "token (at least 32 characters)" -> also at most 128 characters; `producer` at most 128: service.cc:433,445.
  - L58 "`directory` ... Its parent must already exist" -> verified (single-level `create_directory`): service.cc:1444. MISSING: under the shipped systemd unit (`ProtectSystem=strict`) a config-relative directory under `/etc/detent` is read-only; the example must use an absolute path under `/var/lib/detent`: packaging/systemd/detentd.service ProtectSystem=strict, StateDirectory=detent.
  - L76-80 `detentd` flags: MISSING `--reactors N` (default 4), `--runtime-dir`, `--system`, `--config`, env `DETENTD_PORT/REACTORS/RUNTIME_DIR`, and that `--status` prints `running (pid N)` plus the health body and exits 1 when unreachable: src/detentd/main.cc:37-54,109-153.
  - MISSING: `detentd --daemonize` returns when the listener is up, before a single voter accepts `/v2/open` (the example's first `/v2/open` can fail until a leader exists): src/detentd/main.cc:190-193 [OWNED daemon-ready-leader: readiness will wait for a leader; any "retry the open" advice changes].
  - L12-18 "passed native, sanitizer and three-platform qualification" -> see UNVERIFIED.

- `guides/durable-relay.md` | **missing content** | claims verified against src/docs/RELAY.md; one broken relative link, internal "Stage 5B" status wording, no configuration sample [OWNED namespace-s4-s10, observability]
  - L31 "[managed delivery](managed-delivery.md)" -> relative link resolves under `/Detent/guides/durable-relay/`; every other page uses `/Detent/guides/managed-delivery/`: fix the URL.
  - L13 "Hosting has Stage 5B component acceptance" -> internal project-stage wording (status narration); say what runs and what is qualified in plain terms, as on guides/durable-authority.md.
  - L21-29 defaults "1,024 connections and sessions, with 640 polls" -> verified: include/detent_relay/service_limits.hh:6-8; include/detent_relay/configuration.hh:21. Port 8318 verified: src/detent_relay/configuration.cc:90.
  - MISSING: the actual `relay.json` shape (`address`, `port`, `hostname`, `certificate`, `private_key`, `ca`, `bundles[{source,directory,mode:"fresh"|"existing",cache_bytes,retained_edges}]`, `credentials[{token,principal,policy_epoch,bundles}]`, `control_bytes`, `baseline_bytes`, `heap_bytes`, `connections`, `service_limits`): src/detent_relay/configuration.cc:60-62,103-122; docs/RELAY.md:13-110. The page only points at GitHub.
  - MISSING: `--daemonize`, `--runtime-dir`, `relay.pid`/`relay.log`, logging flags (no logging keys in `relay.json`): src/detent_relay/main.cc:17-35,45,62. `--status` queries HTTPS health with CA and hostname, 2 s deadline: main.cc:46-60.
  - MISSING: relay source files always use subscription delivery (explicit `"poll"` rejected; omitted selects subscription): src/detent_relay/configuration.cc:117.

- `guides/kill-switches.md` | **current** | each claim holds on main; code samples use `detent::Client` [OWNED legacy-h, observability]
  - L17-19, L43-46 `client.eval_or_panic(...)` -> `Client::eval_or_panic` exists (include/detent/client.hh:171); OWNED legacy-h: samples change to `Reader` when it lands.
  - L69 "within one poll interval" -> true for a file source (client.cc:293-333); for managed delivery latency is `poll_interval_ms` (default 200) or the subscription long poll, not the client's `poll_interval`: src/detent_delivery/configuration.cc:207-208.
  - L105-109 hint "Detent doesn't track ... a report of knobs that haven't been read" -> true today; OWNED observability (`detent_usage`, `detent-agent --status --usage`, usage page) replaces it with a real report.
  - MISSING: a runnable authority publish for the roll-out and revert steps (a `/v2/transactions` body for `checkout:killswitch_new_path`) and the matching `detent get checkout:killswitch_new_path --hash user-42` read; the page shows only the definition JSON.

- `guides/live-updates.md` | **missing content** | true on main; names only two source kinds and is deleted by legacy-h [OWNED legacy-h]
  - L7 "exactly one source: a local file, or a poll function" -> a third source exists: `InitOptions::read_source` (local generation directory): include/detent/client.hh:118; src/detent/client.cc:112; guides/managed-delivery.md section 5 uses it.
  - Whole page: legacy-h acceptance A14 replaces `guides/live-updates.md` with a reader reference page; the page is deleted when it lands (.campaign/detent/quests/legacy-h.md A14).
  - L97 intervals table -> advice, no code claim (not verified; see UNVERIFIED).
  - L110-118 rules 1-4 -> verified: client.cc:80-81,109-112; default `poll_interval` 200 ms: client.hh:101.
  - L76 "rereads it when its modification time or size changes" -> verified: src/detent/client.cc:300-319.

- `guides/managed-delivery.md` | **wrong** | `image_format` default is `paged-v2`, not `flat-v1`; migration/compat text throughout [OWNED legacy-i, dense-pages, legacy-h, reader-s6, namespace-s3b]
  - L149 "`image_format` | `"flat-v1"`; opt in with `"paged-v2"`" -> default is `PagedV2`; config only overrides when the key is present: include/detent_delivery/coordinator.hh:30; src/detent_delivery/configuration.cc:141-146. OWNED legacy-i/dense-pages: the key and both names are removed (one Dense v3 format; a config naming `image_format` is refused).
  - L175-179 section "Paged local publication" ("Set `image_format: paged-v2`", "Existing configured flat heads migrate", "Older flat-only binaries cannot read v2", "switching the writer back to `flat-v1`") -> opt-in framing is wrong (paged is the default) and the rest is D43 compatibility narration. OWNED legacy-i, dense-pages (refuse-and-refetch is the single path).
  - L72 "`--upgrade-managed` explicitly adopts a compatible legacy generation directory" -> live on main (`StoreOpenMode::UpgradeLegacy`): src/detent_agent/main.cc:102,116; src/detent_local/disk_store.cc:387; the page names it "legacy" (D43 text). Owner call whether the mode survives; no quest removes it.
  - L173 "Opt-in incremental paged publication is under assembled-process qualification" -> not opt-in (see above); status narration.
  - L5 "Instead of polling a JSON snapshot" -> history framing of an old shape.
  - L107-133 section 5 `detent::InitOptions`, `detent::local::make_local_read_source`, `detent::Client`, `client.read_view()` -> all exist (include/detent_local/client_source.hh:30-31; client.hh:192) [OWNED legacy-h, reader-s6]. The prepared path `make_managed_read_source(dir, publisher_uid, limits, identity)` is what `detent get` uses and is not mentioned: client_source.hh:32-34; src/detent_agent/main.cc:242-243.
  - L135-157 resource table: defaults verified for `heap_bytes` 256 MiB, `mapping_bytes` 512 MiB, `disk_bytes` 1 GiB, `disk_entries` 4,096, `disk_generations` 1,024, `poll_interval_ms` 200, `reconnect_maximum_ms` max(30,000, poll), `timeout_ms` 60,000, `compaction_interval_ms` 60,000 (0 or 1,000-86,400,000), `compression` true, `artifact_mode` 384/420: include/detent_delivery/configuration.hh:11-12; src/detent_delivery/configuration.cc:147-150,206-212,216-225,249-252.
  - MISSING: required keys `cluster`, `group`, `epoch`, `principal`, `namespaces`, `token`, `ca`; `policy_epoch` (default 1); `token` 32-128 printable chars; at most 4,096 namespaces of at most 198 chars: configuration.cc:155-175.
  - MISSING: `scrub_bytes_per_second` (default 524,288, 0 disables, max 1 GiB) and the `managed scrub found N damaged packs` warning [OWNED namespace-s3b: repair of damaged packs]: configuration.cc:151-153; include/detent_delivery/coordinator.hh:33; src/detent_agent/managed_host.cc:82.
  - MISSING: `image_limits` object (`records`, `namespaces`, `wire_bytes`, `string_bytes`, `decoded_bytes`, `image_bytes`, `pages`, `segments`, `overrides`, `definition_bytes`): configuration.cc:226-247.
  - MISSING: reader settings `publisher_user`, `reader_validation` (`prepared` default | `full`), `reader_*` capacity (`records`, `namespaces`, `overrides`, `image_bytes`, `string_bytes`, `pages`, `segments`, `heap_bytes`, `mapping_bytes`, `owners`) in `agent.json` or flags, trusted-file rule: src/detent_agent/config.cc:55-67; reader_capacity.hh:7-9; docs/USAGE.md:250-252,273.
  - MISSING: managed config file rules are mode `0600`/`0640` (any bit in `0027` refused), single link, owner root or euid, ACL-free, at most 1 MiB: configuration.cc:67-71.
  - MISSING: foreground vs `--daemonize`, `--system` (foreground only), startup budget of 180 s, `--runtime-dir`: src/detent_agent/main.cc:99-123; config.hh:9.
  - L159-165 "Changing Authorities" `reset_from` -> verified (`cluster`, `group`, `epoch`, `scope`, must change identity): configuration.cc:253-263.
  - L181-189 endpoint pools -> verified: at most 8 distinct nonzero-port IPs, conflicts with top-level `address`/`port`, `quorum_refresh_ms` 1,000-86,400,000 default 30,000, jitter +/-20%: configuration.cc:179-196; src/detent_delivery/source_selection.hh:15-26,51-52.

- `guides/python-and-rust.md` | **stale** | claims about the bindings hold on main, but "not yet"/"original specification" wording is history narration; bindings move to the reader core [OWNED legacy-h]
  - L5 "wrap the same C++ `detent::Client`" -> true today (bindings/python/detent_python.cc:43; bindings/rust/src/lib.rs header comment); OWNED legacy-h: `Reader` over `libdetent_reader` (Python C extension, Rust crate over a C ABI) replaces it.
  - L83-93 "What the Bindings Don't Do Yet ... the original specification describes" -> D43 history text; gaps are true on main (no `read_view`, `get_as`, `_or_panic`, `check()`; Python exposes one `DetentError`): bindings/python/detent_python.cc:41-111; bindings/rust/src/lib.rs:76-130. State what exists, drop "yet"/"original specification".
  - L94 "no wheel on PyPI and no crate on crates.io" -> see UNVERIFIED. Cargo package is `detent-rust` v0.1.0, lib name `detent`: bindings/rust/Cargo.toml:2-9.
  - L19-37, L46-60 Python and Rust samples -> signatures verified (`Client(defaults_path, source_path=None, cli_overrides=[], poll_interval_ms=200)`; `Client::new(&str, Option<&str>, Vec<String>, i32)`): detent_python.cc:48-71; rust/src/lib.rs:81-88.
  - L28-30 health dict keys verified: detent_python.cc:96-103. Build flags `DETENT_BUILD_PYTHON`/`DETENT_BUILD_RUST` default OFF verified: CMakeLists.txt:47-48.

- `guides/shell-and-agent.md` | **missing content** | commands verified; reader flags, trust settings and config search order are not on the page [OWNED legacy-h, reader-s6, reader-s7-s9, observability]
  - L74 table "macOS | `~/Library/Application Support/Detent`" -> `XDG_CONFIG_HOME` (when absolute) is honored first on every platform, including macOS: src/detent_cli/config.cc:21-29.
  - MISSING: when the user `agent.json` does not exist the system config dir is used (`/etc/detent`, `/usr/local/etc/detent`, `/Library/Application Support/Detent`); `--system` reads only the system dir; `--config PATH`: src/detent_cli/config.cc:93-106.
  - L88-98 "Local Defaults" `defaults.json` / `--defaults` -> exists on main (src/detent_agent/config.cc:70-83; main.cc:235) and is also read from env `DETENT_DEFAULTS_PATH`; OWNED legacy-h (A17: `detent get` loses `--defaults` until reader S7) and reader-s7-s9: section is rewritten or removed.
  - MISSING: reader flags `--publisher-user USER_OR_UID`, `--reader-validation prepared|full`, `--reader-records/-namespaces/-overrides/-image-bytes/-string-bytes/-pages/-segments/-heap-bytes/-mapping-bytes/-owners` and env `DETENT_READER_*`; these settings in a file need a trusted regular file: src/detent_agent/main.cc:151-176,312-323; reader_capacity.hh:7-9; src/detent_cli/config.cc:156-177; docs/USAGE.md:250-273.
  - MISSING: `detent get` exit 2 with the usage text when called with no arguments; `help`/`--help`: main.cc:370,373,382-385.
  - L30-36 exit-status table -> `0`/`1` verified; error text `ERR MissingKnob: ... (not present in current snapshot)` verified: src/detent/client.cc:189; main.cc:298.
  - L110-117 `--status` output and SIGTERM recipe -> verified: main.cc:127-140. `--status` exits 1 when the agent is not running: main.cc:145.
  - L121-124 runtime directory and mode `0700` -> verified: src/detent_cli/config.cc:42-47; daemon.cc:57.
  - MISSING: `--initialize-managed`, `--upgrade-managed`, `--daemonize` (not valid with `--system` or `--status`): main.cc:99-105,438.
  - MISSING [OWNED observability]: `detent-agent --status --usage`.

- `guides/testing.md` | **stale** | claims hold on main, but the page documents `TestKnobs`/`detent::global`/`InitOptions` (removed by legacy-h) and carries history narration [OWNED legacy-h]
  - L96-100 hint "The specification Detent grew from describes ... Detent doesn't implement it yet" -> D43 history/"not yet" text; delete.
  - L13-45 `detent::testing::TestKnobs(snap).set_thread_local()`, `detent::global::eval_or_panic` -> exist on main (include/detent/testing.hh:14-26; global.hh:17-45); `Client(Snapshot)` exists (client.hh:130); all removed by legacy-h.
  - L53-69 CLI override rules -> verified: src/detent/overrides.cc:35-47 (lowercase `true`/`false`, else signed base-10 int64), duplicate error overrides.cc:58-60, WrongType client.cc:180,219. Error text names `--knob`, not the application's flag.
  - L78-90 defaults-only client -> verified (client.cc:86-96).
  - MISSING: testing against the real store (a `detent get` against a generation directory) and the shell reader's `--default`; the page has no non-C++ testing story.

- `operations/authority.md` | **missing content** | configuration table verified for the keys it lists; three tunable objects and bounds are absent [OWNED rc5-release-candidate]
  - L35 "`voters` ... IP `host`" -> verified, IP literal only (`asio::ip::make_address`): src/detent_authority/service.cc:381-384. Owner ruling D47 #26 says voter addresses accept DNS names; OWNED rc5-release-candidate: this row and the guide's `host` change when it lands.
  - MISSING: `application_limits` (`keys` 500,000, `namespaces` 4,096, `producers` 1,024, `mutations` 128, `request_bytes` 256 KiB, `state_bytes` 128 MiB, `image_bytes` 256 MiB, `definition_bytes` 16 KiB, `overrides` 256, `results` 256): service.cc:212-234; state.hh:25-29.
  - MISSING: `subscription_limits` (`sessions`, `per_principal`, `idle_ms`, `maximum_ms`, `opens`, `polls`, `closes`, `per_principal_controls`, `per_principal_polls`, `open_bytes`, `control_bytes`, `wait_ms`) with bounds: service.cc:267-298.
  - L43 `delivery_limits` lists `heap_bytes`, `wire_bytes` only -> also `string_bytes`, `records`, `namespaces`, `overrides`, `definition_bytes`, `timeout_ms`: service.cc:299-325. Defaults 128 MiB / 256 MiB verified: service.hh:42; include/detent_distribution/protocol.hh:13.
  - L59-60 credential table -> `token` and `producer` are also capped at 128 characters; `namespaces` must be non-empty: service.cc:433,445,447-448.
  - L45-52 replacement-pool figure 336,931,316 and the quoted error -> verified: tests/detent_authority/test_node.cc:106,115,124. Retained-state defaults come from docs/AUTHORITY_ARCHITECTURE.md:61-72 (library defaults outside this repo; see UNVERIFIED).
  - L92 timing "100 ms heartbeat, 500 ms RPC, 1-2 s elections" -> verified: service.cc:410-413. "TLS below 1.2, early data and renegotiation are disabled" -> docs only (docs/AUTHORITY_TRANSPORT.md:13), see UNVERIFIED.
  - L26-27 credential config rules: config file must be regular, owned by root or euid, no group-write or world access, at most 1 MiB; no single-link check on this file: service.cc:193-196.
  - MISSING: `detentd` health body fields and `--status` JSON (readiness, role); see reference/http-api/health.md.
  - L128-132 `--status` with publisher TLS, CA and name verification, 2 s deadline -> verified: src/detentd/main.cc:80-84; include/detent_transport/https_client.hh:95.

- `operations/logging.md` | **wrong** | the example log lines, `-vv` metric samples and "agent source transitions" describe a pre-managed agent that is not in the code; footprint paragraph is process narration [OWNED observability: logging of usage may be added]
  - L10 `detent-agent: source connected` and L9 `detentd: rejected publish (version)` -> neither string exists in src; real agent lines are `detent-agent: managed local store ready, selected generation N`, `managed delivery running, awaiting its first generation`, `managed publication acknowledged at revision N` (debug), `managed source endpoint ... after N rotations`, `managed quorum refresh failures N`, `publication synced ...`, `managed scrub found N damaged packs ...`, `managed delivery failures N, preserving durable selection: ...`, `managed delivery drained`: src/detent_agent/managed_host.cc:47,50,60,65,70,75,82,88,103,224.
  - L23 "`detent-agent -vv` # also source metric samples, every 200 ms" -> no such output; the only levels are `info` (default), `-v` = debug (adds `publication acknowledged`, `quorum Open succeeded`), `-vv` = capped at the library maximum: src/detent_cli/logging.cc:39; managed_host.cc:60,88.
  - L52 "Agent source transitions: connected, unavailable, and refresh failures. Repeated failures drop to debug until a newer snapshot arrives" -> no such transition or demotion logic; failures log at WARNING each time `state.failures` changes: managed_host.cc:96-104.
  - L56 "HTTP logging omits headers ... Authority and relay HTTP access logging remains pending" -> no HTTP access-log call site in the shipped daemons (only lifecycle lines): `grep L_ src/detentd src/detent_relay src/detent_authority`; src/detentd/main.cc:192,201,212; src/detent_relay/host.cc:306,335,422. See UNVERIFIED. The sentence also narrates pending work.
  - L5, L9 relay is a logging program ("authority daemon, agent and relay log to stderr") -> true (src/detent_relay/main.cc:12); MISSING: `detent-relay` accepts `--log-level`, `-v`, `--color` but has no `relay.json` logging keys: main.cc:12,19.
  - L13 "Tinting is the default; there is no separate tint option" -> verified (hard-coded true): src/detent_cli/logging.cc:61. L69 4,096-entry queue -> verified: logging.cc:64. Default level `info`: src/detent_cli/logging.hh:7.
  - L21-34 flags and level names, `on`/`off`/`24bit`/`256color`/`ansi`/`stacked` aliases -> verified: src/detent_cli/logging.cc:13-52. `-v` repeated is capped at 2: logging.cc:14. `stacked` is accepted but missing from the flag table.
  - L38-40 "The same settings work in `agent.json` and `detentd.json`" -> verified: src/detent_agent/config.cc:48-50; src/detentd/main.cc:92-94. Invalid values error: `invalid log level`, `verbosity must be 0..2`: logging.cc:31,37.
  - L60-67 destinations -> verified: `agent.log` (src/detent_agent/main.cc:108), `detentd-<port>.log` (src/detentd/main.cc:154), `relay.log` (src/detent_relay/main.cc:62); journal under systemd, `/var/log/detent` for launchd and rc.d: packaging/launchd/org.detent.detent-agent.plist, packaging/rc.d/detent_agent.
  - L73-79 "Footprint": "The original logger addition cost `detent-agent` 88,280 bytes" -> verified (387,240 -> 475,520 stripped): benchmarks/logging-footprint.json:37,59. "The later ... 88 bytes": benchmarks/xapiand-logging-preliminary-20261020.json:359. 0.51/0.52 s medians, 47.0/47.5 s per million polls: benchmarks/logging-cost-comparison-20261021.json:14,18,338,360. Numbers are true; the wording ("original", "later", "accepted exploratory comparison", "component review accepts the file-size exception") is history/process narration (D43): state the current size and cost only.
  - MISSING: `detentd` startup line names mode and transport: `durable {single-voter|fixed-voter} authority listening with {HTTP|HTTPS} on ADDR:PORT`; the page shows only the HTTP single-voter form: src/detentd/main.cc:163,192.

- `operations/security.md` | **wrong** | "single-link" claimed for every token-bearing config file but only the managed credential file is checked; no at-rest-encryption statement; reader trust model missing [OWNED rc5-release-candidate, legacy-h]
  - L32-33 "Configuration files holding tokens must be regular, single-link files" -> `st_nlink == 1` is checked only for the managed credential file; `authority.json` checks regular file, owner, no group-write, no world access, at most 1 MiB, no link count: src/detent_delivery/configuration.cc:68 vs src/detent_authority/service.cc:193-196; `agent.json`/`detentd.json` likewise without it: src/detent_cli/config.cc:156-161.
  - MISSING: owner ruling D47 #25: Detent has no at-rest encryption; knob values are not secrets; use disk or volume encryption. The page never says so (nor does it say voter replication is mutual TLS with pinned certificates, which it does state at L22-24). OWNED rc5-release-candidate (landing text per .campaign/detent/quests/rc5-release-candidate.md:38) and docs-site-usage.
  - L15-17 "A **file source** accepts any valid snapshot in its file" -> true of the C++ file source: src/detent/client.cc:293-333; OWNED legacy-h (file source removed).
  - L11-12 "A system agent's snapshot is world-readable (0644) by design" -> default `artifact_mode` 0644 for system, 0600 for users; `artifact_mode` may be 0600/0640/0644: src/detent_delivery/configuration.cc:249-252.
  - L10 "A user agent's runtime directory is private (0700)" -> verified: src/detent_cli/daemon.cc:57.
  - L24-27 mutual TLS plus per-voter SHA-256 pin, publishers over HTTPS when not loopback -> verified: src/detent_authority/service.cc:371,385-400,422-423. Per-voter pin is of the DER leaf (docs).
  - L36-37 "Managed agents verify the authority's certificate chain and hostname" -> verified: configuration.cc:199-205; `ca` required and hostname default `localhost`.
  - MISSING: the reader trust model: managed reads default to `reader_validation: prepared` and authenticate the publisher by `publisher_user` (euid for user agents, `detent`/`_detent` for system), never by the directory's owner; reader settings in a file need a trusted regular file (no ACL-added rules, no group/other write, no final symlink): src/detent_agent/config.cc:13-42,55-56; src/detent_cli/config.cc:156-177; docs/USAGE.md:250-252,273; docs/SERVICES.md "Reader capacity and service credentials".
  - MISSING: `GET /v1/health` is unauthenticated and must not carry a body: src/detent_authority/service.cc:716-722.
  - MISSING: token limits (32-128 chars, unique), one in-flight command per producer, request body cap 256 KiB: service.cc:433,445,856; state.hh:27.
  - MISSING: managed agent artifact privacy, `0600` credential file, and relay downstream tokens (32-128 printable bytes): docs/RELAY.md:65.
  - L39-43 vulnerability reporting link -> not checkable from the repo (see UNVERIFIED).

- `operations/services.md` | **missing content** | templates and paths verified against packaging/; reader trust account, log controls and authority data location omitted
  - L12-20 layout table and L22-27 mode rules -> verified: src/detent_cli/config.cc:34-89; daemon.cc:57-58; docs/SERVICES.md:5-13.
  - L26-30 `detentd.json` / `agent.json` one-liners -> verified keys `authority` and `managed_path`: src/detentd/main.cc:97; src/detent_agent/config.cc:53.
  - L64-67 "units run as `detent`, use `ProtectSystem=strict`, `ProtectHome`, `PrivateTmp` and `NoNewPrivileges`" -> verified: packaging/systemd/detentd.service, detent-agent.service. Both units declare `RuntimeDirectory=detent`.
  - MISSING: where the authority's `directory` goes: `ProtectSystem=strict` makes `/etc/detent` read-only, so `authority.json` needs an absolute `directory` under `/var/lib/detent` (the unit's `StateDirectory=detent`, 0700): packaging/systemd/detentd.service; src/detent_authority/service.cc:354-357.
  - MISSING: `publisher_user` in the public `agent.json` (numeric UID of the service account; defaults `detent` on Linux/FreeBSD, `_detent` on macOS), which system readers need for prepared validation: src/detent_agent/config.cc:13-22; docs/SERVICES.md "Reader capacity and service credentials".
  - MISSING: managed startup and automatic reads allow up to 180 s: src/detent_agent/config.hh:9.
  - MISSING: foreground/`--daemonize`/`--status` conventions (`--system --daemonize` and `--status --daemonize` fail): src/detent_agent/main.cc:101,438; src/detentd/main.cc:134-135; docs/SERVICES.md "Foreground, background, and status".
  - MISSING: log controls (`log_level`, `verbosity`, `color` in the service JSON; `color: "never"` when the manager allocates a terminal): src/detent_agent/config.cc:48-50; docs/SERVICES.md "Log controls".
  - L128-134 managed state `/var/lib/detent-agent` (0755) and `/var/db/detent-agent` -> verified: src/detent_cli/config.cc:81-89; packaging/systemd/detent-agent.service StateDirectory=detent-agent; packaging/rc.d/detent_agent.
  - L136-138 relay paragraph and "Hosting remains under Stage 5B qualification" -> internal stage name (status narration); MISSING: relay state parents `/var/lib/detent-relay` (Linux) and `/var/db/detent-relay`, absolute bundle directories, `--config`/`relay.json`: docs/RELAY.md "System services".
  - L49-53 install step `cp .../systemd/*.service` also copies `detent-relay.service`: packaging/systemd/ lists three units.

- `operations/upgrading.md` | **delete** | the page is compatibility and migration narration for a greenfield project (D43); its table is also wrong
  - L5 "Detent has no tagged releases yet ... compatibility boundaries to know when you update a deployment built from `main`" -> D43: no compatibility notes before release.
  - L16 "Local generation images | 1 | Old SDK readers can't consume managed leases" -> on main images are layout 1/2 (paged v2 default), Dense v3 is landing; there is no live "version 1" reader boundary: include/detent_delivery/coordinator.hh:30; docs/LOCAL_IMAGE_FORMAT.md:211. OWNED dense-pages, legacy-i: a store in any other format takes one refuse-and-refetch path.
  - L24 "Upgrade every reader to a version with dual legacy/managed support before enabling managed publication" -> D43 migration text; no such mechanism is described anywhere in the code.
  - L24-25 `--upgrade-managed` -> see guides/managed-delivery.md (live flag, "legacy" wording).
  - L30-34 "C++ ABI ... rebuild every consumer" -> generic; `InitOptions`/`HealthSnapshot` are removed by legacy-h [OWNED legacy-h].
  - L12 `schema_version: 1` "Stable" -> `schema_version` must equal 1 and is rejected otherwise (a current rule, belongs on reference/snapshot-format.md): src/detent/json_schema.cc:280-284.

### 4. Configuration and limits

- `reference/configuration.md` | **wrong** | L5 claim false, default image format wrong, a stale runtime-dir file, whole classes of keys and env vars unnamed [OWNED legacy-h, legacy-i, dense-pages, rc5]
  - L5 "Every Detent setting has a built-in default, so nothing needs configuring for a local setup" -> false: `detent get` and `detent-agent` fail with `no source configured` (src/detent_agent/main.cc:104-105, 305) and `detentd` fails with `--authority CONFIG is required` (src/detentd/main.cc:136).
  - L6 "collects every file, directory and environment variable" -> not every one: `DETENT_READER_*` (10 vars, src/detent_agent/reader_capacity.cc:25-28), `DETENTD_RUNTIME_DIR` (src/detentd/main.cc:103), `HOME` (src/detent_cli/config.cc:24, 61) are absent from the table at L137-146.
  - L99 "`image_format` as `"flat-v1"` (default) or `"paged-v2"`" -> the default is `paged-v2`: `LocalImageFormat image_format = LocalImageFormat::PagedV2` include/detent_delivery/coordinator.hh:30; the key is only read when present, src/detent_delivery/configuration.cc:141-146. [OWNED legacy-i: the key, both names and the "Recovery reads either format" text are deleted; dense-pages: one format]
  - L99 "Recovery reads either format ... Deploy compatible readers before selecting paged writing" -> per-format and compatibility narration (D43); live on main (src/detent_agent/managed_host.cc:159) but removed by legacy-i. [OWNED legacy-i]
  - L63, L73, L80-81, L141 `defaults_path`, `defaults.json` auto-discovery, `DETENT_DEFAULTS_PATH` -> exist on main: src/detent_agent/config.cc:51, 70-83 (loader only; `detent-agent` never reads it), src/detent_agent/main.cc:235 (`detent get` only), include/detent/client.hh:100 `InitOptions::defaults_path`. [OWNED legacy-h: rewrite when the Client and the defaults file are deleted; legacy-h A17 drops `--defaults` from `detent get` until reader-s7-s9]
  - L157 "System agent runtime directory ... `0644` pid and snapshot" -> there is no snapshot file in the runtime directory; it holds `agent.pid` (0644 system / 0600 user, src/detent_cli/daemon.cc:58) and, with `--daemonize`, `agent.log` (0600, src/detent_cli/daemon.cc:97).
  - L160-161 "regular, single-link file without world access or group/other write" -> true for `managed.json` and relay.json (src/detent_delivery/configuration.cc:67-71: single link, `!(mode & 0027)`, ACL check, at most 1 MiB); `authority.json` has no link-count or ACL check, only regular + owner + `!(mode & 0007)` + `!(mode & 0022)`, at most 1 MiB (src/detent_authority/service.cc:191-196). `agent.json`/`detentd.json` are not permission-checked unless they carry a trusted key (src/detent_cli/config.cc:156-161, 177).
  - L154 "`agent.json`, `detentd.json` | root | readable by the service account" -> code does not require root ownership: owner root or the effective user, no group/other write, only for the trusted keys (src/detent_cli/config.cc:158-159, 177).
  - L32-38 "Service State | System `/var/lib/detent`, `/var/db/detent`" -> no code path reads it. `default_state_dir` only feeds the user agent default `<state>/agent` (src/detent_cli/config.cc:81-83; sole caller src/detent_agent/main.cc:412). The system values are packaging conventions (packaging/systemd/detentd.service:12 `StateDirectory=detent`); the authority directory is the `directory` key.
  - L46-51 "Managed Agent State" lists only the system column -> user value is `<Service State user dir>/agent`, e.g. `~/.local/state/detent/agent`: src/detent_cli/config.cc:81-83. Used as `--generation-dir` whenever `--managed` is set and no directory is given: src/detent_agent/main.cc:411-412.
  - L169 "Destinations must be distinct IP addresses" -> distinct endpoints (address plus port), nonzero port, not unspecified or multicast: src/detent_delivery/source_selection.hh:28-30; IP literals only (`asio::ip::make_address`, src/detent_delivery/configuration.cc:177, 189).
  - L103-106 `authority.json` has no key table -> the page only links out. The owner ruling (D47 #26) wants voter addresses documented as DNS names, but the code accepts IP literals only: `asio::ip::make_address(text(field(voter, "host")))` src/detent_authority/service.cc:381 (a hostname throws; "voter address" check at :382-383). Do not write "DNS names" until a quest changes this line; no quest does (rc5 lists it only as an input, quests/rc5-release-candidate.md:38). [OWNED rc5]
  - MISSING: `agent.json` keys `publisher_user` (name or UID, trusted-file only; default euid, `detent`/`_detent` for system) and `reader_validation` (`prepared` default | `full`, trusted-file only): src/detent_agent/config.cc:55-56, 13-30; src/detent_agent/main.cc:90-93.
  - MISSING: `agent.json` keys `reader_records`, `reader_namespaces`, `reader_overrides` (0-256), `reader_image_bytes`, `reader_string_bytes`, `reader_pages`, `reader_segments`, `reader_heap_bytes` (default 8 MiB), `reader_mapping_bytes` (default 512 MiB), `reader_owners` (default 16,384); flag `--reader-*`, env `DETENT_READER_*`, positive integers (zero only for overrides): src/detent_agent/config.cc:62-67, src/detent_agent/reader_capacity.hh:8-10, src/detent_agent/reader_capacity.cc:28-35, 45, 50-52.
  - MISSING: config file bounds: 64 KiB, JSON nesting 16, unknown and duplicate fields rejected, relative paths resolved only for path keys: src/detent_cli/config.cc:155, 162, 175, 179, 195-197. Managed/relay/authority files are 1 MiB: src/detent_delivery/configuration.cc:70, src/detent_authority/service.cc:196.
  - MISSING: removed option `detentd --workers` / `workers` / `DETENTD_WORKERS` is rejected with "was removed": src/detent_cli/config.cc:112-125, 172-174, called at src/detentd/main.cc:89.
  - MISSING: `authority.json` keys other than `subscription_limits`: `application_limits` (keys, namespaces, producers, mutations, request_bytes, state_bytes, image_bytes, definition_bytes, overrides, results; defaults src/detent_authority/state.hh:26-29), `journal_limits`, `retained_state`, `delivery_limits` (heap_bytes, wire_bytes, string_bytes, records, namespaces <= 65536 then <= 4096, overrides <= 256, definition_bytes <= 16 KiB, timeout_ms), `storage_backend`, `consensus_limits`: src/detent_authority/service.cc:207-209, 213-355. `voters` accepts 3, 5 or 7 (odd, at most `consensus.voters` = 7, kronuz/consensus ab05507:include/consensus/types.h:56): src/detent_authority/service.cc:373-375.
  - MISSING: `managed.json` keys with no reference or guide row: `image_limits` (records, namespaces, wire_bytes, string_bytes, decoded_bytes, image_bytes, pages, segments, overrides, definition_bytes), `scrub_bytes_per_second` (default 512 KiB/s, 0 disables, max 1 GiB/s), `policy_epoch` (default 1): src/detent_delivery/configuration.cc:152-153, 159, 226-247; include/detent_delivery/coordinator.hh:33.
  - MISSING: `relay.json` is not documented on the site (only a link to docs/RELAY.md at L165): keys address, port (8318), hostname, certificate, private_key, ca, bundles (1-256: source, directory, mode `existing`|`fresh`, cache_bytes, retained_edges 1-256), credentials (1-10,000), control_bytes (64 MiB; 16 MiB-1 GiB), baseline_bytes (16 MiB; 1 MiB-1 GiB), heap_bytes, connections, service_limits: src/detent_relay/configuration.cc:58-60, 63-66, 90, 95-97, 100, 103, 122-123, 139; include/detent_relay/configuration.hh:13-21.

- `reference/limits.md` | **missing content** | every number on the page matches a constant; the Client rows are Client-only and the image rows lack the paged bounds [OWNED legacy-h, dense-pages, legacy-i, rc3]
  - L14 name 1-200 bytes -> include/detent/validation.hh:22-23 (true). L15 group 1-128 -> :35-36 (true).
  - L23-25 Client rows: 100,000 knobs -> include/detent/client.hh:103 and include/detent/json_schema.hh:19 (true); 16 MiB -> include/detent/json_schema.hh:20 (true); poll interval positive -> src/detent/client.cc:80-82 (true). `InitOptions` and `Client` exist on main; the section is deleted or rewritten when legacy-h lands. [OWNED legacy-h]
  - L31 "3 minutes" -> `managed_startup_timeout = 180 s`: src/detent_agent/config.hh:9 (true).
  - L40-44 managed agent: 256 MiB heap include/detent_delivery/configuration.hh:11; 512 MiB mapped :12; 1 GiB / 4,096 / 1,024 disk src/detent_delivery/configuration.cc:223-225 (all true).
  - L50-54 image: 256 MiB, 128 MiB strings, 500,000 records, 4,096 namespaces, 256 overrides -> include/detent_local/view.hh:28-29, include/detent_distribution/protocol.hh:13-14 (true). Section title "Local Image Format" names a per-format limit set. [OWNED dense-pages, legacy-i]
  - L58-62 consensus 65,536 entries / 64 MiB, `retained_state` independent -> kronuz/consensus ab05507:include/consensus/types.h:58-59 (true; pin in cmake/dependencies.json:179), src/detent_authority/node.hh:17-23.
  - MISSING: paged image bounds `pages` 1,100,000 and `segments` 4,096, page 64 KiB, segment 1 MiB: include/detent_local/paged_layout.hh:9, 11. [OWNED dense-pages]
  - MISSING: authority limits not on any limits row: 1,024 in-flight requests and publisher connections (src/detent_authority/service.hh:40, src/detentd/main.cc:34), 16 MiB queued bytes (service.hh:40) and reply bytes (:41), 5 s timeout (:47), delivery heap 128 MiB (:42), `application_limits` defaults (src/detent_authority/state.hh:26-29).
  - MISSING: agent and managed bounds: reader capacity defaults (heap 8 MiB, mapping 512 MiB, owners 16,384; src/detent_agent/reader_capacity.cc:50-52), `reactors` 1-65,535 (src/detentd/main.cc:133), config files 64 KiB / 1 MiB (src/detent_cli/config.cc:155), managed token 32-128 chars, principal <= 128, namespaces <= 4,096 of <= 198 bytes (src/detent_delivery/configuration.cc:161-163, 172-175), relay bundles 256 and credentials 10,000.
  - MISSING: subscription limits with their maxima live only on configuration.md; this page has no row (src/detent_authority/service.cc:279-297).

### 5. Reference and the rest

- `reference/cli.md` | **missing content** | true as far as it goes, but names three programs and omits `detent-relay`, whose binary, flags and logging flags exist [OWNED daemon-ready-leader]
  - L5 "Detent installs three programs" -> a fourth, `detent-relay`, is built with `DETENT_BUILD_RELAY` (default OFF, CMakeLists.txt:43), installed (src/CMakeLists.txt:97-101) and shipped with units (packaging/systemd/detent-relay.service). No `reference/cli/detent-relay.md` exists on the site; src/detent_relay/main.cc:18-35 is its whole CLI: `--config FILE`, `--system`, `--daemonize`, `--status`, `--runtime-dir DIR`, logging flags, `--help|-h`; `relay.json` default location :43; `relay.pid`/`relay.log` :45, 62.
  - L35 "Both daemons accept the same logging flags" -> `detent-relay` too (src/detent_relay/main.cc:12); `detent` does not (the reader build never parses them, src/detent_agent/main.cc:390-406 and `validate_get_args` :151-177).
  - L42 `--color auto|always|never` -> also `on`, `off`, `truecolor`/`24bit`, `256`/`256color`, `16`/`ansi`, `stacked`: src/detent_cli/logging.cc:40-52. `--log-level=VALUE`, `--color=VALUE`, `--verbosity=VALUE` forms: :20-22. `-vvv` clamps at two steps: :13-14.
  - L23-25 "`--daemonize` ... reports readiness or the startup error" -> true (src/detent_cli/daemon.cc:75-142); startup timeout is 30 s for `detentd`/`detent-relay` (src/detent_cli/daemon.hh:29) and 180 s for the agent (src/detent_agent/main.cc:119). Not on the page.
  - L26-27 "`--status` is read-only" -> true for `detentd` (src/detentd/main.cc:143-153), agent (src/detent_agent/main.cc:125-146), relay (src/detent_relay/main.cc:46-59). `detent` has no `--status`.
  - L28-29 "`--system` ... stops `detent get` from ever starting a private agent" -> src/detent_agent/main.cc:267 (true).
  - MISSING: exit statuses: `detent` with no arguments or no knob name prints usage to stderr and exits 2 (src/detent_agent/main.cc:370, 383-384); every other failure exits 1; `detentd` `--help` exits 0.
  - MISSING: removed-option rejection (`detentd --workers`) -> src/detent_cli/config.cc:117-125.

- `reference/cli/detent.md` | **wrong** | the error line goes to stderr, not stdout; one environment variable claim is wrong; usage omits reader flags [OWNED legacy-h, reader-s7-s9]
  - L57 "Read failed | Stdout `ERR <Category>: <name> (<detail>)`" -> stderr: `std::fprintf(stderr, "ERR %s%s\n", ...)` src/detent_agent/main.cc:298-299; a trailing ` (no durable local generation available)` is appended when no generation was read.
  - L56 "Read failed, `--default` given | The default | 0" -> not when the category is `SourceFailure` (a damaged or unverifiable page): then the error prints and the exit status is 1: src/detent_agent/main.cc:294.
  - L74-75 "`DETENT_DEFAULTS_PATH` ... configure a newly started agent" -> it configures only `detent get`'s own fallback (src/detent_agent/main.cc:235). `launch_agent` drops `--defaults` (:203) and the agent never reads a defaults file.
  - L64-65 example `ERR MissingKnob: checkout:nope (not present in current snapshot)` -> that detail text is the `Client` path (src/detent/client.cc:189); a read of a present local generation reports `(local image lookup failed)` (src/detent_local/view.cc:25-27). See Unverified.
  - L28, L46 `--defaults` "typed fallback knobs" -> exists on main (src/detent_agent/main.cc:235-236, 283-285); legacy-h A17 removes it from `detent get` until reader S7. [OWNED legacy-h, reader-s7-s9]
  - L6-10 synopsis -> matches `kUsage` src/detent_agent/main.cc:312-315, but the code's accepted value flags also include `--publisher-user USER_OR_UID`, `--reader-validation prepared|full`, `--reader-records|--reader-namespaces|--reader-overrides|--reader-image-bytes|--reader-string-bytes|--reader-pages|--reader-segments|--reader-heap-bytes|--reader-mapping-bytes|--reader-owners N` (src/detent_agent/main.cc:154-167, 320-322). MISSING from the page.
  - L24 "`--hash VAL`", `--switch` -> true; every value flag rejects a value starting with `--`: src/detent_agent/main.cc:174-175.
  - L30 "`--system` Attach to the system agent; never start one" -> true (src/detent_agent/main.cc:267). L41-43 start rule (`--managed` set, user mode, no generation present, sibling `detent-agent` else PATH) -> :196-200, 267-275 (true).
  - MISSING: `--generation-dir` defaults to `<user state>/agent` when only `--managed` is set: src/detent_agent/main.cc:411-412. MISSING: env `DETENT_READER_*` (src/detent_agent/reader_capacity.cc:25-28).

- `reference/cli/detentd.md` | **missing content** | flags, defaults, env and `--status` text match; help, config-key rules and the startup readiness change are not on it [OWNED daemon-ready-leader]
  - L6-8 synopsis, L21-28 options -> match src/detentd/main.cc:39-41, 115-130 (all eight flags present; `--help|-h` at :223-226 is not listed).
  - L12, L22 default `8317`, `0` binds a free port -> src/detent_cli/config.hh:10, src/detentd/main.cc:68-69, 131-132 (true). L23 `--reactors` default 4 -> :133 (true); range 1-65,535 is missing (:59-60, 133).
  - L72-73 "Without `--authority`, `--status` probes `127.0.0.1`" -> also true when `detentd.json` supplies `authority` (settings merge, src/detentd/main.cc:91-100, 140-142, 75); only a run with neither probes loopback.
  - L75-77 two-second deadline, `not reachable` -> include/detent_transport/https_client.hh:95, 111; src/detentd/main.cc:149-151 (prints `not reachable: <reason>` to stdout, exit 1). True.
  - L30-31 "PID and log filenames include the port" -> `detentd-<port>.pid`, `.log`: src/detentd/main.cc:138, 154. True.
  - L61-62 "`--authority` or the `authority` field must name a configuration" -> required except with `--status`: :136.
  - L35-41 env table -> src/detentd/main.cc:102-108 (true).
  - MISSING: `--daemonize` returns when the listener is up (src/detentd/main.cc:190-193); `daemon-ready-leader` changes this to "after a leader accepts /v2/open" and `--status` `"ready"` likewise. [OWNED daemon-ready-leader]
  - MISSING: `--status` and `--daemonize` are mutually exclusive, `--system --daemonize` rejected: src/detentd/main.cc:134-135. Publisher connection cap 1,024: :34.

- `reference/cli/detent-agent.md` | **stale** | `--upgrade-managed` is a legacy-adoption mode (D43); reader and publisher flags, default generation directory and environment are missing [OWNED legacy-h]
  - L26 "`--upgrade-managed` Adopt a compatible older generation directory" -> `StoreOpenMode::UpgradeLegacy` (src/detent_agent/main.cc:116, include/detent_local/disk_store.hh:9). It is live on main, but it is a per-version adoption path that D43 forbids; no quest removes it. Owner call needed (keep and document as a current mechanism, or delete code, flag and text).
  - L25 `--initialize-managed` "first start only" -> `StoreOpenMode::FreshEmpty` (:115); with neither flag the mode is `ExistingManaged` (:117). The two flags are mutually exclusive: :102-103.
  - L14, L35-41 behavior, L41 rejections -> src/detent_agent/main.cc:101, 104-105, 438 (true).
  - L47-51 `--status` sample and L53-56 -> src/detent_agent/main.cc:125-146 (true: `running (pid N)` / `not running`, `durable revision=R knob_count=N`, or `local generation unavailable: <why>`; exit 0 only when running).
  - L24 `--generation-dir PATH` "Durable generation directory" -> default `<user state>/agent`, system `/var/lib/detent-agent` (Linux) or `/var/db/detent-agent`: src/detent_cli/config.cc:81-89; src/detent_agent/main.cc:411-412.
  - L60-66 env table -> true (src/detent_agent/main.cc:408-410). MISSING `DETENT_READER_*`.
  - MISSING options: `--publisher-user USER_OR_UID`, `--reader-validation prepared|full`, `--reader-*` capacity flags (src/detent_agent/main.cc:397-401, 345-353), `--quiet` (accepted, used by `detent get` to launch the agent: :201, 394), logging flags `--log-level`, `-v`, `--verbosity`, `--color`, `--no-color` (src/detent_agent/main.cc:325-326, 391).
  - MISSING: `--status` with reader capacity and `--publisher-user` validates prepared generations: src/detent_agent/main.cc:133-138. [OWNED reader-s7-s9]
  - MISSING: startup readiness timeout 180 s with `--daemonize` (src/detent_agent/main.cc:119, src/detent_agent/config.hh:9).
  - L36 `agent.pid`/`agent.log` -> src/detent_agent/main.cc:68, 108 (true; log is 0600, `--daemonize` only).

- `reference/knobs.md` | **current** | the model, name rule, definition fields and group-name bounds match the code; the read column names the Client API [OWNED legacy-h]
  - L12-16 types and refinements -> src/detent/json_schema.cc:64-128, 151-160 (true). L88 "Unknown fields are rejected" -> :128, :160 (true). L35-39 name rule and 200 bytes -> include/detent/validation.hh:22-31 (true). L93 groups 1-128 -> :35-36 (true).
  - L14-15 "Read with `eval`, `get`, `get_as<T>`" and L18-19 `WrongType` -> `detent::Client` methods exist on main (src/detent/client.cc:180, 195, 219, 233; include/detent/client.hh:148). [OWNED legacy-h: replaced by the Reader read calls when legacy-h lands]
  - L117-118 "when a live snapshot is merged over baked defaults" -> baked-defaults path is the C++ defaults file (include/detent/client.hh:100); [OWNED legacy-h].
  - MISSING: per-definition bounds `overrides` at most 256 and definition at most 16 KiB (include/detent_distribution/protocol.hh:14-15; src/detent_authority/state.hh:29), duplicate override key rejected (src/detent/json_schema.cc:109-112).

- `reference/errors.md` | **missing content** | the five read categories and seven rejection reasons exist, but a sixth read category and 23 HTTP error names are absent; the Client sections go with legacy-h [OWNED legacy-h]
  - L16-22 category table -> `ErrorCategory` has six values; `SourceFailure` is missing: include/detent/errors.hh:8-15, src/detent/client.cc:45, src/detent_local/view.cc:357, 384 (returned when a local page fails verification or a generation vanishes; `detent get` does not apply `--default` to it, src/detent_agent/main.cc:294). [OWNED namespace-s3b: damaged packs map to `SourceFailure`]
  - L18, L70-76 `detent::global`, `Client` constructor throws, baked defaults, CLI override -> exist on main (include/detent/global.hh:31, src/detent/client.cc:79-110); every Client row is rewritten when legacy-h lands. [OWNED legacy-h]
  - L54-62 update rejection reasons -> include/detent/errors.hh:27-35 (all seven true). These are `Client` updater reasons.
  - L41-42 "`detent get --default VALUE` exists because a script has no baked defaults file of its own" -> `detent get` also accepts `--defaults` on main (src/detent_agent/main.cc:235). [OWNED legacy-h]
  - L87-92 authority errors -> true as far as it goes (`outcome_unknown` 503, result names src/detent_authority/service.cc:153-155, statuses :171-180). MISSING: the other HTTP error names the services return, none of which any page names: `admission_full` 503 (src/detent_authority/service.cc:665, 670, 693), `stopping` 503 (:851), `unauthorized` 401 (:618, 640), `publish_forbidden` 403 (:644), `scope_forbidden` 403 (:1873; relay src/detent_relay/service.cc:112), `producer_busy` 429 (:856), `authorization_timeout` 503 (:1900), `distribution_unavailable` 503 (:2165), `invalid_request` 400 with `detail` (:1114), `body_bound` 413 (:744), `unknown_endpoint` 404 (:833), `source_not_leader` (:165), `not_found` 404 and `internal_error` (relay src/detent_relay/service.cc:109, 491), `source_behind` (relay :151).
  - MISSING: bindings errors `detent.DetentError` (bindings/python/detent_python.cc:28-43) and Rust `DetentError(String)` (bindings/rust/src/lib.rs:67); CLI error forms `ERR <Category>: <name> (<detail>)`, `detent: <message>`, `config <path>: <message>` (src/detent_agent/main.cc:298, 447; src/detent_cli/config.cc:132) and the exit statuses 0/1/2.

- `reference/observability.md` | **missing content** | the Client health and metrics match the code; what the daemons export today (relay health, agent logs, authority histograms) is missing; the planned design is not there [OWNED legacy-h, observability]
  - L12-19 `client.health()` fields -> include/detent/client.hh:49-57 (true); `staleness_seconds` semantics -> src/detent/client.cc:266, 280. L30-39 counters -> include/detent/client.hh:36-45 (all eight true). L47 `refresh_alert_after` 5 minutes -> :102 (true). `Client`, `health()`, `Metrics` are deleted by legacy-h A16; the whole Client half is rewritten then. [OWNED legacy-h]
  - L55-61 "doesn't export them. There's no Prometheus or OpenMetrics endpoint" -> true today: `git grep -i prometheus|openmetrics|textfile` finds nothing in src, include or bindings. The design in docs/design/observability.md (Q3, D47) adds `detent-agent --status --usage` JSON and a Prometheus text file with no listener, `detent_usage`, and a usage page; OB* quests do not exist yet. [OWNED observability: the hint's "no exporter" and the "your own metrics library" advice change when OB4 lands]
  - L58-59 metric names borrowed from an internal working spec -> a public page should not name it or its prefix; the page now names the `Metrics` counters. [OWNED observability]
  - L67 "`GET /v1/health` on the authority" -> src/detent_authority/service.cc:716, 1066 (true).
  - L68 "`detentd --status` and `detent-agent --status`" -> src/detentd/main.cc:143-153, src/detent_agent/main.cc:125-146 (true).
  - MISSING: `detent-relay --status` and the relay's `GET /v1/health` (same path, HTTPS; src/detent_relay/main.cc:46-59, src/detent_relay/service.cc:359, 405): fields `ready`, `heap_used`/`heap_peak`/`heap_limit`, `sessions`, `opens`/`polls`/`closes`, `bootstrap`, `catchup`, `disk_*`, per-bundle `revision`, `ready`, `authenticated`, `fenced`, `gaps`, `refusals`, `source_endpoint_index`, `source_rotations`, `quorum_refresh_failures`, `scrub_*`, `sync_*`: src/detent_relay/host.cc:59-127; rebuilt every 100 ms (host.cc:332, 345).
  - MISSING: the authority health document carries `kronuz::metrics` duration histograms (`duration_json`, src/detent_authority/service.cc:56-67; listed on reference/http-api/health.md) and is rebuilt every 250 ms (:2584).
  - MISSING: what the managed agent logs (these are its only live observability signals): ready/awaiting-first-generation (src/detent_agent/managed_host.cc:47-50), source endpoint rotations (:65), quorum-refresh failures (:70), publication sync counts (:75), scrub damaged packs (:82), delivery failures with the preserved selection (:103), drained (:224).
  - L71-75 "Hash values, switch values and knob values are never logged" -> the 13 `L_*` calls in agent, delivery, local and authority sources log revisions, counts and endpoints only (e.g. src/detent_agent/managed_host.cc:47-103); a full proof is a grep job (see Unverified).

- `reference/cpp-api.md` | **missing content** | every stated type/header exists on main; header table omits public headers; whole page describes Client, `InitOptions`, `global`, `TestKnobs`: OWNED legacy-h (rewrite as a Reader page when it lands; the lead decides)
  - MISSING: public headers not in the table: `detent/json_schema.hh` (`parse_defaults`, `parse_source_snapshot`, `kDefaultMaxKnobs`=100'000, `kDefaultMaxBytes`=16 MiB: include/detent/json_schema.hh:19-20), `detent/json_patch.hh` (`parse_snapshot_patch`, `apply_patch`, `diff_snapshots`: include/detent/json_patch.hh:35-60), `detent/hashing.hh` (`stable_bucket`: src/detent/hashing.cc:7), `detent/cursor.hh`.
  - MISSING: `detent_local/client_source.hh` also exports `make_managed_read_source`, `LocalReadSource`, `ClientSourceOptions`: include/detent_local/client_source.hh:6-35.
  - L23 `detent::local_client` target and L14 `detent::detent` target exist: src/detent_local/CMakeLists.txt:8, src/CMakeLists.txt:34 (true; C++20 at src/CMakeLists.txt:30).
  - L43-46 "Errors are exceptions at startup... throws" true: src/detent/client.cc:81,89,93,101,110,113.

- `reference/cpp-api/client.md` | **wrong** | `refresh_alert_after` has no effect and `Metrics` are per-Client, not process-wide; rest matches include/detent/client.hh: OWNED legacy-h (Client, `InitOptions`, `health()`, `Metrics`, views all deleted; replaced by `Reader`)
  - L45 "`refresh_alert_after` ... Staleness threshold for alerting" -> the field is stored and never read; nothing alerts: include/detent/client.hh:102,232, src/detent/client.cc:78,154 (no other reference in src/ or include/).
  - L138 "`Metrics` holds process-wide atomic counters" -> one `Metrics` per `Client` (`mutable Metrics metrics_` member): include/detent/client.hh:234 (the header comment at :33 says "process-wide" too, wrongly).
  - L79 "`get` ... `switch_val` accepted and ignored" true: src/detent/client.cc:204 (parameter commented out).
  - MISSING: error order. `InvalidName` is checked first, before the CLI override: src/detent/client.cc:167,207; the read-source path counts only ok/missing/wrong_type (not conversion): src/detent/client.cc:28-35.
  - MISSING: `ClientReadView::cursor()` and `EvaluationView`/`ReadSource`/`ReadSourceStatus` interfaces for a custom `read_source`: include/detent/read_view.hh:15-63.
  - MISSING: `read_source` constructor errors: `std::invalid_argument("null local read source")` and the conflict `invalid_argument`; `source_path`+`source_poll` conflict is `std::runtime_error` (page says only "mutually exclusive"): src/detent/client.cc:110,113,128.

- `reference/cpp-api/global-and-testing.md` | **stale** | signatures and behavior match; the hint narrates the "specification it grew from" (D43 history text): OWNED legacy-h (`detent::global`, `TestKnobs`, `TestGuard`, `OverrideTable` deleted; this page is replaced by the reader reference per legacy-h A14)
  - L99-103 "The specification it grew from describes a repeatable `--knob name=value` flag..." -> history narration; also no code ships a `--knob` flag (only the error strings in `parse_cli_overrides` mention it): src/detent/overrides.cc:18,25,52.
  - L28-30 `init` twice throws `std::logic_error`, read before `init` is `Uninitialized`: true, src/detent/global.cc:24-26,38-41.
  - L47-57 `TestKnobs`/`TestGuard` signatures true: include/detent/testing.hh:15-45.

- `reference/cpp-api/sources.md` | **stale** | behavior matches; the file-source hint narrates the spec it grew from; `ClientSourceOptions` and backoff constants missing: OWNED legacy-h (file/poll/managed sources and `make_local_read_source` deleted)
  - L28-31 "The specification Detent grew from describes watching the parent directory..." -> history narration (D43).
  - L24-26 file source (mtime+size poll, 16 MiB) true: src/detent/client.cc:300-326, include/detent/json_schema.hh:20.
  - L57 "backs off with full jitter" true; MISSING the numbers: delay uniform in [0, min(30 s, 250 ms x 2^attempt)], exponent capped at 20: src/detent/client.cc:385-396.
  - MISSING: `ClientSourceOptions` fields `limits` (`ViewLimits`), `identity` (`Cursor`), `poll_interval` (200 ms, 1 ms..24 h else `invalid_argument`), `auto_refresh` (default true), `prepared_publisher`: include/detent_local/client_source.hh:6-14, src/detent_local/client_source.cc:105-108.
  - MISSING: `make_managed_read_source(dir, publisher, limits, identity)` and `LocalReadSource::refresh()` (manual refresh when `auto_refresh=false`): include/detent_local/client_source.hh:25,32.

- `reference/bindings.md` | **current** | every claim true on main (wraps `detent::Client`, pybind11/cxx, CMake options): OWNED legacy-h, reader-s7-s9 (bindings become a C extension and a crate over the C ABI; "Technology" and "wrap the C++ `detent::Client`" change)
  - L9-12 options and dirs true: CMakeLists.txt:47-48, bindings/CMakeLists.txt:7-13.
  - L22-25 hint "no direct reader for the agent's generation images outside C++" true on main; becomes false when the C ABI lands (reader-s6, legacy-h).

- `reference/bindings/python.md` | **current** | signature, errors, `health()` dict match bindings/python/detent_python.cc: OWNED legacy-h, reader-s7-s9 (`Client`/`DetentError` become `Reader`/`Status`/`ReaderError`/`Lookup` on a C extension)
  - L17-22 constructor args and defaults true: bindings/python/detent_python.cc:46-68; L26 construction errors raise `DetentError`: :55-65; L72-80 gaps true (no read_view/get_as/_or_panic; module has only `Client`, `DetentError`): :43-104.
  - L5-10 `DETENT_BUILD_PYTHON` true: CMakeLists.txt:47, bindings/python/CMakeLists.txt:13.

- `reference/bindings/rust.md` | **current** | API matches bindings/rust/src/lib.rs; one nit: `HealthInfo` is not nameable from the crate root: OWNED legacy-h, reader-s7-s9 (cxx replaced by a crate over the hand-declared C ABI, no C++ in cargo)
  - L14-31 signatures true: bindings/rust/src/lib.rs:81-129; L62 `DetentError(pub String)` true: :67.
  - MISSING: `health()` returns `ffi::HealthInfo` from a private `mod ffi`, so callers cannot write `detent::HealthInfo`; the page presents it as a top-level type: bindings/rust/src/lib.rs:12,127.
  - MISSING: Cargo package is `detent-rust`, library name `detent`: bindings/rust/Cargo.toml:2,8; build needs a prior C++ build dir (`DETENT_BUILD_DIR`): bindings/rust/build.rs:19-21, bindings/rust/CMakeLists.txt:13.

- `reference/evaluation.md` | **missing content** | precedence, bucket function and golden example values verified; omits the `InvalidName` check that runs first and the `Uninitialized` view case: OWNED legacy-h (L5-6 "C++ client", L13/L56 `eval`/`get` Client signatures, L63 `get_as`, L117 `read_view()`; CLI-override step 1 exists only on Client/`ClientReadView`, not in `detent::local::evaluate`)
  - MISSING: step 0, invalid name -> `InvalidName` before the CLI override lookup: src/detent/client.cc:167,207, src/detent/read_view.cc:18,35; a default-constructed view returns `Uninitialized`: src/detent/read_view.cc:25,42.
  - L36-47 order (override, lookup, group, rollout, default) true: include/detent/detail/evaluation.hh:9-22, src/detent/client.cc:161-199; group rollout without hash returns default: evaluation.hh:16-17.
  - L69-76 bucket: src/detent/hashing.cc:7-21; L107-108 `bucket(...)` = 10 and 2 verified by computing SHA-256 of `name\0hash` (10, 2).
  - L96 golden vectors in `tests/detent/test_hash_golden.cc` exists: tests/detent/test_hash_golden.cc:1-30. L70-93 agree with docs/mechanisms/evaluation.md:9-13 (mechanism doc has no CLI-override step; it describes the local evaluator).

- `reference/http-api.md` | **wrong** | "every /v2 request names cluster, group, epoch" is false for session next/close, and replies carry an `authority` envelope, not bare `{"error":...}`; status-code table absent
  - L30-31 "Every `/v2` request names the authority's `cluster`, `group` and `epoch`" -> `session/next` takes `version, session, after, published, wait_ms` and `session/close` takes `version, session`: src/detent_distribution_http/session_request.cc:160-170.
  - L27 "A rejected request returns `{"error":"..."}`" -> handler replies are wrapped as `{"authority":{cluster,group,epoch},"error":...}`; only admission-time rejections (401/403/413/503 from `create_extension`/`on_request_body`) are bare: src/detent_authority/service.cc:988-994,1124 vs :617,644.
  - MISSING: status-code table. 400 `invalid_request` (+`detail`; also wrong method and foreign identity): :754,1114; 401 `unauthorized`: :617,640; 403 `publish_forbidden`: :644; 404 `unknown_endpoint`: :833; 413 `body_bound` (session lanes) and `reply_bound` (read reply): :744,2417; 429 `producer_busy`: :856; 503 `admission_full`, `stopping`, `outcome_unknown`, `authorization_timeout`, `distribution_unavailable`, `source_not_leader`: :1003-1030,1900,2165,165.
  - MISSING: the relay serves `POST /v2/distribution/session/{open,next,close}` and `GET /v1/health` (404 for anything else, 403 `scope_forbidden` for unknown credential, 503 `source_behind`): src/detent_relay/service.cc:106-122,131-157,359-374; no HTTP reference page covers them.
  - L5-6, L8-11, L26 verified: `detentd --authority CONFIG` src/detentd/main.cc:5; TLS when `publisher_tls`: :164-168; 256 KiB default: src/detent_authority/state.hh:27; `request_bytes` is configurable (`application_limits`): src/detent_authority/service.cc:212-231.

- `reference/http-api/authority.md` | **wrong** | outcome table mislabels `conflict`/`identity_conflict`/`invalid`; identity rule false for session next/close; response shape and status codes missing; "existing"/"replaces" history wording
  - L17-18 "Includes the exact authority identity: `cluster`, `group`, `epoch`" (every `/v2` request, L13) -> not on `session/next` and `session/close`: src/detent_distribution_http/session_request.cc:160-170. Identities must also be nonzero lowercase hex: src/detent_authority/service.cc:101-118.
  - L113 "`conflict` | A precondition failed, or a retry changed its contents" -> a retry with a different body (same sequence) is `identity_conflict`; `conflict` is only a failed precondition: src/detent_authority/state.cc:926-935,944-950.
  - L118 "`invalid`, `identity_conflict` | Malformed request, or wrong authority identity" -> `invalid` is never produced (Status enum starts at 1): src/detent_authority/state.hh:50-61; malformed or foreign-identity requests are HTTP 400 `invalid_request`: src/detent_authority/service.cc:756-760,1114; `identity_conflict` means same sequence, different canonical body: state.cc:931-932.
  - L129-130 "returns `ResultUnavailable`" -> wire token is `result_unavailable` (L116 spells it correctly): src/detent_authority/service.cc:152-155.
  - L5-7 "replaces whole-snapshot publishing" and L193 "The existing distribution endpoint retains automatic snapshot fallback" -> comparison to a removed shape (no whole-snapshot publish route exists); D43 history wording.
  - L144 "Oversized selections return `reply_bound`" true but unstated: HTTP 413: src/detent_authority/service.cc:2417.
  - MISSING: transaction/open/close/result response body `{"authority":{...},"status":"<outcome>","revision":N,"namespace_revision":N,"generation":N,"sequence":N}`: service.cc:151-160; HTTP status 200 (`applied`, `no_change`), 429 (`capacity`), 409 (all others incl. `conflict`, `session_fenced`, `out_of_order`, `result_unavailable`, `identity_conflict`, `exhausted`): :171-180.
  - MISSING: `/v2/read` reply also carries `schema_version: 1` and `version` (applied revision), `knobs` is the snapshot-shaped map; keys must lie in `namespace`, be unique, and `namespace` must be authorized (else 400): src/detent_authority/read_reply.cc:73-106, service.cc:812-826.
  - MISSING: transaction validation: `operations` non-empty, unique keys, `absent_namespace_revision` required exactly when `expected_revision` is 0 (and rejected otherwise): src/detent_authority/state.cc:209-217.
  - MISSING: `coding` is `raw` or `zstd`, `profile` must be 1, `namespaces` names up to 198 bytes, `scope` 64 hex digits: src/detent_distribution_http/session_request.cc:129-150; `X-Detent-Coding` is `raw`/`zstd`: src/detent_distribution_http/reply_pump.cc:79.
  - MISSING: session `wait_ms` defaults to the configured maximum (default 30000, max 60000): session_request.cc:165-167, src/detent_authority/service.hh:46, service.cc:297; 403 `scope_forbidden`, 503 `admission_full` for session errors: service.cc:1873-1886.
  - MISSING: all limits (L199-212) are defaults overridable under `application_limits`/`subscription_limits`/`delivery_limits`: src/detent_authority/service.cc:212-231,264-330.
  - L199-212 limit values verified: keys 500000/namespaces 4096/producers 1024/ops 128/request 256 KiB/definition 16 KiB/overrides 256/results 256: state.hh:26-29; 16 per producer: service.cc:690; 1024 slots, 16 MiB: service.hh:40; one outstanding per producer (`producer_busy`): service.cc:856; 64 per Raft entry: state.cc:1021,1054; 5 ms window: service.cc:2447.
  - L25-26 `publish:false` gets 403 on open/close/transactions: service.cc:643-645. L20-21 duplicate JSON fields rejected at any depth: :128-148.

- `reference/http-api/health.md` | **missing content** | all stated fields and the 400 rule verified; omits the `authority` envelope on the health body and many fields the document carries
  - MISSING: the health reply is also wrapped with `"authority":{cluster,group,epoch}` (`enveloped` runs on every buffered reply, health included): src/detent_authority/service.cc:988-994,1124 (runtime probe in UNVERIFIED).
  - MISSING: document fields `connections_active/high_water/accepted/rejected`, `requests`, `reserved_body_bytes`, `reserved_reply_bytes`, `read_*`, `distribution_export_*`, `peer_*`, `checkpoint_busy`, `storage_async`: src/detent_authority/service.cc:1581-1656.
  - L22 `mode` values, L28 `fenced`-only document after failure: service.cc:1223-1225; L17 250 ms cadence and publish at ready: service.cc:2584,1712. L11 400 `health_body_forbidden`: :716-722.
  - MISSING: the relay has its own `GET /v1/health` (200 with its document, 503 `{"ready":false}` before one exists; no envelope): src/detent_relay/service.cc:405-414.

- `reference/snapshot-format.md` | **wrong** | JSON field rules verified; claims an HTTP endpoint returns diffs (none does), omits that a patch cannot carry `published_at`, and under-specifies `rollout_pct` and the wire frame
  - L58-59 "...or an HTTP endpoint returns as a diff" -> no route returns a patch; patches enter only through `source_poll` payloads (`is_patch`): src/detent/client.cc:368, grep of src/ shows `diff_snapshots`/`parse_snapshot_patch` used only in src/detent/ (the `PATCH /v1/snapshot` and `X-Detent-Base-Version` comments in include/detent/json_patch.hh:7-15 describe routes that do not exist).
  - L60 "uses the snapshot shape with two differences" -> a third: `published_at` is an unknown top-level field in a patch: src/detent/json_patch.cc:59-77.
  - L95-96 "`rollout_pct` outside `0`-`100`" -> also: must be a JSON integer; `null` is accepted and means unset: src/detent/json_schema.cc:115-125; group override `{"rollout_pct": N}` must have exactly that one member: :99-106.
  - L5-6, L33-35, L41-43 (`schema_version` exactly 1, `version` positive, defaults reject `version`/`published_at`), L88-98 strictness list true: src/detent/json_schema.cc:224-345, 214-222; nesting depth 64: include/detent/json_schema_internal.hh:28; 16 MiB and 100,000 defaults: include/detent/json_schema.hh:19-20; `published_at` check is `YYYY-MM-DDTHH:MM:SSZ` shape only: json_schema.cc:214-222.
  - L107-112 wire format true: MessagePack inside `DTDIST01` frames, 96-byte header, 64 KiB fragments, raw or zstd level 3: src/detent_distribution/framing.cc:11,51-60, include/detent_distribution/framing.hh:7. MISSING (wire vs image): the wire frame header and `UpdateKind` 1 snapshot / 2 delta / 3 progress (include/detent_distribution/protocol.hh:11) are not described; the local image (`DTIMAGE1`, `DTPAGE02`/`DTROOT02`: src/detent_local/layout.cc:8, src/detent_local/paged_layout.cc:232,262) is a separate on-disk format the page rightly does not describe. OWNED dense-pages: when DTPAGE03/DTROOT03 land, any image text added here must say one format; the wire frame is unchanged by that quest.

- `faq.md` | **wrong** | two answers contradict main (C++ "HTTP endpoint directly", the `cluster` library); the rest is true on main today [OWNED legacy-h, reader-s7-s9]
  - L37 "A C++ process can poll a file or an HTTP endpoint directly" -> `InitOptions` has `source_path` (file) and a caller-supplied `source_poll` function only; no built-in HTTP source (`detent_http` removed, CHANGELOG.md:70; `--source-http` gone): include/detent/client.hh:62, :99. The Live Updates page says "a file, or a poll function you supply" (website/src/content/docs/guides/live-updates.md:5).
  - L64-67 "Detent's durable consensus work lives in the same `cluster` library" -> Detent has no `cluster` pin; consensus is `Kronuz/consensus`, the cluster pin left with the classic Raft: cmake/dependencies.json:177, CHANGELOG.md:29. Also L64 "Raft clustering" extracted from Xapiand: the dependency list has `consensus` and `journal` (cmake/dependencies.json:165,177), not a Xapiand-shared `cluster`.
  - L7-8, L12-15, L17-21, L37-39 describe the C++ `detent::Client`: poll interval 200 ms (include/detent/client.hh:101), `staleness_seconds` (client.hh:53), baked defaults (`defaults_path` client.hh:60), full-jitter backoff (src/detent/client.cc:349-397). All exist on main; legacy-h deletes the Client, so the answers on freshness, outage, baked defaults and "Do I need the agent?" must be rewritten around `Reader` when it lands.
  - MISSING: managed delivery has `delivery_mode` `poll` (default) or `subscription`, which changes the freshness answer: src/detent_delivery/configuration.cc:132-140, include/detent_delivery/coordinator.hh:20. The agent's poll default is `poll_interval_ms` 200: configuration.cc:208.
  - MISSING: no FAQ entry on encryption at rest (owner ruling D47 #25: knob values are not secrets, use disk or volume encryption; operations/security.md has no such text), on the 500,000-knob authority ceiling (src/detent_authority/state.hh:26), or on how a non-C++ process reads (`detent get`, src/detent_agent/main.cc:312).

- `help.md` | **current** | both repository links and the two internal links are valid; GitHub Issues/Discussions availability is not checkable from the tree (see UNVERIFIED)
  - Internal links `/Detent/welcome/` and `/Detent/faq/` resolve to website/src/content/docs/welcome.md and faq.md.
  - MISSING (optional): no pointer to the contributor design documents (website/src/content/docs/architecture/design-documents.md) or to the self-diagnostics a reporter needs (`-v` / `-vv`, src/detent_cli/logging.cc:13; `detent-agent --status`, src/detent_agent/main.cc:125-141; `GET /v1/health`, src/detent_authority/service.cc:716).

- `next.md` | **wrong** | the quick-start teardown is correct, but the "file or an HTTP endpoint" link text contradicts the code and the page it links; does not duplicate another page [OWNED legacy-h, reader-s6]
  - L33-34 "point clients at a file or an HTTP endpoint" -> sources are a file or a poll function: include/detent/client.hh:62, :99; guides/live-updates.md:5. The same phrase is in guides.md ("feed clients from a local file or an HTTP endpoint"), so fix both.
  - L31-32 "link the library, ship baked defaults, and read knobs" describes `detent::Client` and `defaults_path` (client.hh:59-61, :121); legacy-h removes them, so this bullet must be rewritten when it lands.
  - L16-20 teardown commands are true: `agent.pid` src/detent_agent/main.cc:68, `detentd-<port>.pid` src/detentd/main.cc:138, output lines "running (pid N)" and "durable revision=N knob_count=N" src/detent_agent/main.cc:127-140. reader-s6 may change the `--status` read path.
  - Overlap: L29-47 re-lists links that guides.md already groups (Reading Knobs / Running the Servers); it is the quick-start's last page (quickstart.md:195), so keep it, and keep the two link sets identical.

- `roadmap.md` | **wrong** | lists as missing two things main has (CI, a change hook), uses a superseded workload, and keeps "legacy" and "Xapiand compatibility" text; omits the work the campaign now schedules [OWNED legacy-h, legacy-i, dense-pages, reader-s6, reader-s7-s9, namespace-s3b, namespace-s4-s10, async-drivers, observability, rc1-rc5]
  - L51-52 "Continuous integration on Linux, macOS and FreeBSD, including the sanitizer runs" listed as remaining -> .github/workflows/ci.yml:20 runs Linux GCC/Clang x86_64 and arm64, macOS, ASan/UBSan and TSan (`tsan: true` :26); only FreeBSD is off (`freebsd: false` :25). Also jobs source-checks (:52), pin-check (:75), read-counters (:86), benchmarks (:37).
  - L77 "A change-notification API for applications that want to react to updates" listed as missing -> `InitOptions::on_update` exists: include/detent/client.hh:115 (documented at architecture/update-path.md:74). It dies with the Client (legacy-h); the replacement is the reader change file (reader-s5, landed).
  - L32 "Legacy Raft safety corrections | Accepted; Xapiand compatibility retained" -> classic Raft and the distributor were removed (CHANGELOG.md:29, commit 8340b53); D43 forbids "legacy"/compatibility text.
  - L39-41 "planning workload ... 500,000 knobs, 300 concurrent publishers, 1,000 transactions per second and 50,000 agents" -> docs/SCALE_TARGETS.md:3,9 supersedes it (5M knobs, 10k+ publishers, 250k+ agents, sub-millisecond reads); PLAN.md Goals keep those targets.
  - L83 "migration ... Opt-in agent/relay format configuration" and L106 "paged delivery" -> `image_format` flat-v1/paged-v2 (src/detent_delivery/configuration.cc:141-146) is removed by legacy-i, and Dense v3 pages replace the paged v2 path (dense-pages).
  - L106 "Controlled contention attributes the native 500k feedback failure to a fixture observer deadline" -> sourced from docs/STAGE6_ARCHITECTURE.md:46, but D45 records that the 500K paged cases fail on main's v2 paged path itself (261109-175635-3c8fd9); this is history narration (D43) and must go.
  - L14-23 "Client library ... **Done**" and "Host-local agent and `detent get` **Done**" are true on main (tests/detent/test_hash_golden.cc, test_duplicate_precedence.cc, test_concurrency_stress.cc) but change under legacy-h, reader-s6 and reader-s7-s9 (`detent get` on the core; Python and Rust over the C ABI).
  - L87-88 "A metrics endpoint (Prometheus/OpenMetrics) for the daemons" -> D47 Q3 settles a Prometheus textfile plus `detent-agent --status --usage`, no listener (docs/design/observability.md); rewrite when observability lands.
  - L72-74 Python/Rust gaps are true (bindings/python/detent_python.cc:45-50 file source only; bindings/rust/src/lib.rs:58-127 `eval`/`get`/`health` only). MISSING from the roadmap: the managed generation source for both bindings and Rust `get_as::<T>` / panicking helpers, which reference/bindings/python.md:73 and rust.md:69-70 list as unimplemented.
  - Items true on main (no row): L50 M1 pins done (CHANGELOG.md:125); L53 tap builds from source (packaging/homebrew/detent.rb:1, :11-14); L55 no `--version` in any CLI usage text (src/detent_agent/main.cc:312-358, src/detentd/main.cc:37-47); L59-64 only bool/int knobs, int has no overrides (include/detent/model.hh:25-30); L68-71, L76 absent (src/detent/overrides.cc:18 parses `--knob` pairs only; no inotify/FSEvents, src/detent/client.cc:288).
  - MISSING: RC-1 to RC-5 exits and M1b/N1/Dense ordering (PLAN.md Goals); the owner-approved reader (`libdetent_reader`), namespace delivery S4-S10, observability, and coroutine drivers appear nowhere on the page.

- `contributing.md` | **missing content** | every claim is true; the page omits the real build profile, the CI gates and the docs checks [OWNED docs-e2e]
  - MISSING: the default configure leaves the relay off (CMakeLists.txt:43) while CI builds `-DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_MANAGED_AGENT=ON` (.github/workflows/ci.yml:22); the page's `cmake -B build` therefore skips the relay tests. Other options: `DETENT_BUILD_PYTHON`, `DETENT_BUILD_RUST` (CMakeLists.txt:47-48), `DETENT_READ_COUNTERS` (:50).
  - MISSING: required gates a change must pass beyond ctest: `python3 tools/check_pins.py` and `python3 -m unittest discover -s tools -p 'test_*.py'` (ci.yml:75-84), source-checks forbidding the removed coroutine/worker paths (ci.yml:52-73), the benchmarks build (ci.yml:37-50), read-counters (ci.yml:86-100).
  - MISSING: the docs check and docs-as-E2E that D44/D52 add (every flag, key, route, error and knob type a page names must exist in source; each page command runs); `campaign config tested_docs` names `website/src/content/docs/**`, so edits to pages void E2E evidence. Also website prerequisites: Node >=22.12 (website/package.json:7), D2 v0.9.0 in CI (.github/workflows/pages.yml:42), the rule that every `unimplemented` hint also appears on roadmap.md (website/README.md:18-19).
  - L63-64 "Pushing to `main` deploys the site" -> only pushes touching `website/**` or pages.yml deploy (.github/workflows/pages.yml:8-10). Verified true: L55-60 `npm ci`, `npm run dev`, `npm run build` (website/package.json:10-11), Astro + Starlight + Pagefind + D2 fences + Edit link (website/astro.config.mjs:23, :36-38, :69-71), L36-37 ASan/UBSan and TSan (ci.yml:20, :26), L58 base `/Detent` (astro.config.mjs).

- `copyright.md` | **missing content** | the licence statement is true; third-party notices and licences are absent
  - L5 "Copyright (c) 2026 Germán Méndez Bravo ... MIT" matches LICENSE:1-3; link target `blob/main/LICENSE` exists in the tree. L7-10 an affiliation disclaimer about another product; removed with D66.
  - MISSING: the build fetches 35 pinned repositories, five of them third-party (asio, cppcodec, rapidjson, lz4, zstd) plus OpenSSL as a system dependency (cmake/dependencies.json:3-6, :33, :93, :183, :189; docs/DEPENDENCIES.md:7), yet the tree has only one licence file (`git ls-files` shows LICENSE only) and no notices page. bindings/rust/Cargo.toml has no `license` field.
  - MISSING: the site loads Lato and Montserrat from Google Fonts (website/astro.config.mjs:65) and ships the Kronuz theme; neither is credited beyond L12-14 naming the theme.
Part 2: top-level guides `docs/ACTORS.md` to `docs/DISTRIBUTION_ARCHITECTURE.md` (30 files). Only ACTORS carries a `> Status:` banner (docs/ACTORS.md:1); the other 29 carry their own date/status lines and no HISTORY banner. "Record" below means the file is dated or status-labelled as a record of a past decision or run.

- `docs/ACTORS.md` | **missing content** | true on main today and checked against source, but omits the landed trusted-restart file and describes the Client and two image formats that are being removed [OWNED legacy-h, legacy-i, dense-pages, namespace-s3b]
  - L22, L36, L129 `detent::Client`, `InitOptions`, `InitOptions::read_source` exist (include/detent/client.hh:59, :118; include/detent_local/client_source.hh:32); L105-111, L116-117 `image_format` `paged-v2` default / `flat-v1` exist (src/detent_delivery/configuration.cc:141, include/detent_delivery/coordinator.hh:30); legacy-i A1 requires `git grep` over docs to find none of them.
  - MISSING: the generation table (L100-109) lacks `generation-<id>.live` and the paced scrub; the agent refuses a store without it: docs/LOCAL_IMAGE_FORMAT.md:258, src/detent_local/trusted_restart.cc, `scrub_bytes_per_second` configuration.cc:152. Verified true: ports/limits (state.hh:26-29), 180 s startup (src/detent_agent/config.hh:9), 60 s compaction (configuration.cc:147), peer mTLS with pinned SHA-256 (service.cc:207, :385).

- `docs/ASYNC_AUTHORITY_ARCHITECTURE.md` | **stale** | opens with "The next deliverable is a real three-process authority"; that authority has shipped [OWNED async-drivers]
  - L5 "next deliverable is a real three-process authority using authenticated sockets ... asynchronous client responses" -> detentd runs fixed-voter mTLS authorities (src/detentd/main.cc:163, src/detent_authority/service.cc:366-407).
  - L150 "additive, move-only deferred HTTP reply capability for the next component" -> that path is removed and CI forbids it: .github/workflows/ci.yml:66 (`DeferredReply`, `should_offload`).
  - L111-144 name library commits ("Cluster `325637b`") and the Worker API; Worker is deleted by async-drivers (M1b), and `cluster` is no longer a pin (CHANGELOG.md:29).

- `docs/AUDIT.md` | **stale** | the opening and "Deployment limits" present the shared-memory agent, the distributor and `shm_format.hh` as live; all were removed
  - L3 "agent lifecycle and shared-memory layout, HTTP polling, distributor persistence and HTTP routes, Raft adapter" and L11 "The production agent's legacy shared-memory path uses a seqlock ... `shm_format.hh`" -> removed in 4f49f4b (CHANGELOG.md:46-50); classic distributor and Raft removed (CHANGELOG.md:29).
  - L11 "immutable local views remain under Stage 4 integration" -> stage-4-distribution landed 2026-10-09; the per-fix sections below it (L39+) are dated history and can stay under a HISTORY banner.

- `docs/AUTHORITY_ARCHITECTURE.md` | **stale** | status and "Loopback daemon profile" describe the first single-voter loopback service, not the shipped authority [OWNED async-drivers]
  - L5 "Authenticated multi-node sockets are the next service component" and L80 "This profile has one voter ... binds `127.0.0.1`" -> fixed 3/5/7 voters and non-loopback publishers with TLS exist: src/detent_authority/service.cc:366-375, :422-423.
  - L80 "does not serve legacy `/v1/snapshot` distribution ... that integration follows" -> the authority serves `/v2/distribution` and subscription sessions: src/detent_authority/service.cc:1085; L101 "bounded migration/export round trip" -> `detent-authority-admin` and its test were removed (CHANGELOG.md:26-27). Limits (500,000 keys, 256 results, 128 mutations) verified: state.hh:26-29.

- `docs/AUTHORITY_COMPONENT_REVIEW.md` | **current** | dated 2026-10-06 consolidated review; its recommendations name `on_update` and legacy JSON polling as history of a decision
  - L32 "retain legacy JSON polling through an explicit adapter" records the Stage 4 decision; the shared-memory path was later removed (CHANGELOG.md:46). No removed target or path is presented as live; no broken links.

- `docs/AUTHORITY_EXPORT.md` | **stale** | status says Stage 5A is implementing what has landed, and L83 contradicts the same page's status
  - L3 "Stage 5A is implementing authenticated subscriptions and snapshot/history handoff" -> stage-5a-subscriptions landed 2026-10-10 (state.json); L43 "Subscription transport and durable-publication feedback still need integration" and L83 "production agent integration is still pending" -> the managed agent exists (src/detent_agent/managed_host.cc) and `/v2/distribution` serves sessions (service.cc:1085).
  - L43 "the legacy endpoint continues its existing snapshot fallback behavior" -> there is no `/v1/snapshot` route (only `/v1/health`, service.cc:716).

- `docs/AUTHORITY_READINESS.md` | **stale** | "Implementation is in progress" for a component accepted 2026-10-06
  - L3 "Implementation is in progress; the checks below are acceptance gates" and L103 "This implementation is under qualification" -> docs/AUTHORITY_COMPONENT_REVIEW.md:3 records Source GO; checkpoint policy exists (src/detent_authority/node.cc:503-516).
  - L37 "Preserve legacy public APIs ... Xapiand's cluster wire behavior" is a constraint on a library Detent no longer pins (cmake/dependencies.json has no `cluster`).

- `docs/AUTHORITY_SCHEDULING_ARCHITECTURE.md` | **current** | dated 2026-10-25 blueprint, labelled "implementation and comparative qualification pending"; no removed name appears [OWNED async-drivers]
  - Not settled from code: whether any of its component plan was implemented (see UNVERIFIED). Owner loops move onto coroutine drivers under async-drivers, which will supersede its `Node::service()` text.

- `docs/AUTHORITY_TRANSPORT.md` | **stale** | points at a source directory and a test that no longer carry what it says
  - L5 "The transport code lives in `src/detent_authority/transport/`" -> that directory holds only https_client.hh; peer transport comes from `consensus::transport` (src/detent_authority/CMakeLists.txt:22-24; commit 7487c5e). `DTHL0001` and `DTC1` appear nowhere in the Detent tree.
  - L31 "The real TCP test remains registered as `test_authority_transport_tls`" -> not registered; the tests are `test_authority_transport_peers` and `test_authority_transport_https` (tests/detent_authority/CMakeLists.txt:56, :62). L31 "three-process failure/scale tests remain unqualified until service integration is complete" is a forward status the service has passed.

- `docs/BOUNDED_APPLICATION.md` | **current** | reference for the bounded Node application; reproduction command and artifact verified
  - `detent_authority_durable_bench NEW_DIRECTORY 100000 64` matches benchmarks/detent_authority_durable_bench.cc:34; `benchmarks/application-quanta-baseline.json` exists. L19 "gates are being rerun after integration" is undated progress narration (minor).

- `docs/BTREE_COMPACT_ROOT_ARCHITECTURE.md` | **current** | dated 2026-10-22 record; "Adopted at `19db54c`" is a fact about cpp-btree/Detent then
  - Detent no longer uses btree at all: the tables are `detent::flat_map` (include/detent/model.hh:27,55,82; docs/CONTAINER_SELECTION_ARCHITECTURE.md:3); no btree reference remains in src, include, cmake or tests. The page never says so.

- `docs/BTREE_ERASE_ARCHITECTURE.md` | **stale** | status "runtime and cost gates open" and "Detent's `noexcept` retirement paths must migrate with this change" for a container Detent dropped
  - L7 and L11 status row (2026-10-21) are forward-looking; docs/BTREE_RELEASE_RISK.md:3 resolves the whole line of work by adopting `detent::flat_map`, and no btree code is in the tree.

- `docs/BTREE_EXCEPTION_SAFETY_ARCHITECTURE.md` | **current** | labelled "Historical design" and records its rejected cost gate (L3)
  - Nothing to change beyond keeping the label; its links to the sibling BTREE records resolve.

- `docs/BTREE_NONFULL_INSERTION_ARCHITECTURE.md` | **current** | dated 2026-10-22 record ("Adopted at `cbc3576`")
  - Same note as COMPACT_ROOT: the adoption is upstream history; Detent now uses `flat_map` (cmake/dependencies.json:51).

- `docs/BTREE_OWNERSHIP_MOVE_ARCHITECTURE.md` | **stale** | status "Reviewed candidate `25f9494` in qualification ...; not adopted"
  - L3 is an open qualification for a container the release no longer uses (docs/BTREE_RELEASE_RISK.md:3; CONTAINER_SELECTION_REPORT.md:3); add a Resolved/superseded status.

- `docs/BTREE_RELEASE_RISK.md` | **current** | status "Resolved 2026-10-24 by adopting `detent::flat_map`" with a link to the decision
  - This is the file that closes the BTREE series; the other BTREE pages should link to it.

- `docs/BTREE_STRUCTURAL_INSERTION_ARCHITECTURE.md` | **stale** | status "On hold pending the model-container comparison"; the comparison concluded 2026-10-24
  - L3 -> docs/CONTAINER_SELECTION_ARCHITECTURE.md:3 "Decided 2026-10-24: `detent::flat_map` for all four fields".

- `docs/CAPACITY_QUALIFICATION.md` | **stale** | the "Boundary / Current behavior" table lists components that no longer exist as current [OWNED legacy-i, dense-pages]
  - L23 "Classic distributor | Has a count option" and L24 "Legacy agent image | Compiled 4 MiB payload" -> both removed (CHANGELOG.md:29, :46); no "4 MiB" payload limit remains in src or include.
  - L66 "Use paged-v2" -> `image_format`/`paged-v2` are removed by legacy-i (A1 greps docs too).

- `docs/CAPACITY_RUNTIME.md` | **current** | the configuration keys it shows exist; the sample names the format key being removed [OWNED legacy-i, dense-pages]
  - L40 `"image_format": "paged-v2"` is accepted today (src/detent_delivery/configuration.cc:141); legacy-i makes it an unknown-key error (A2). `image_limits` keys incl. `pages`, `segments` verified at configuration.cc:228-245; `application_limits`, `journal_limits`, `delivery_limits` at src/detent_authority/service.cc:209-252; `tests/detent_system/presets/capacity-5000000.json` exists. `pages`/`segments` change meaning under Dense v3.

- `docs/CHECKPOINT_POLICY.md` | **current** | describes the latched due condition that the code has (src/detent_authority/node.cc:503-516)
  - L1 "now remains latched" and the closing "ongoing readiness integration" are mild history narration.

- `docs/COMPLETION_ALLOCATION_ARCHITECTURE.md` | **current** | dated 2026-10-07 blueprint for the `completion` library; no removed Detent name appears
  - "legacy overloads"/"Legacy Journal graphs" (L17, L36, L40) refer to the completion library's own API, not to a removed Detent path. Dependency `completion` is pinned (cmake/dependencies.json:159).

- `docs/CONSOLIDATION_RESPONSE.md` | **current** | dated 2026-10-19 response to a cpp-libraries audit; names library APIs (`hashes::crc32c`, `acquire_directory_owner`) as the subject
  - Status "coordinated implementation pending" is not settled from code (M1 moved every pin, CHANGELOG.md:125); "legacy"/"migration" (L10, L22, L28) concern old-binary lock exclusion, not Detent features.

- `docs/CONTAINER_SELECTION_ARCHITECTURE.md` | **current** | status "Decided 2026-10-24"; `detent::flat_map` and the four fields exist (include/detent/model.hh:27,55,82, include/detent/overrides.hh:22)
  - L11 table row "classic distributor publication and legacy shared-memory serialization" names removed consumers as the options of that date.

- `docs/CONTAINER_SELECTION_REPORT.md` | **stale** | status and "Gaps and pending work" say final macOS CI is pending; the quest landed green
  - L4 "Final macOS CI is pending", L120-123 "Gaps and pending work", L173 "Its result is pending" -> container-selection landed 2026-10-24 on hosted run 40125876413, green on six jobs (state.json).
  - L48, L118 compare "shared-memory bytes" and L179 builds the `detent-reader` target; history of a removed path. Test name `test_agent_http` no longer exists.

- `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md` | **stale** | prescribes legacy JSON and shared-memory modes that were removed, and a Client read seam that legacy-h deletes [OWNED legacy-h, reader-s6]
  - L55 "Retain legacy JSON as a named compatibility mode ... Managed delivery replaces `Client::on_update -> ShmWriter`" -> `ShmWriter` and the shared-memory agent are gone (CHANGELOG.md:46-50); D43 forbids a compatibility mode.
  - L78-82 `ClientReadView`, `InitOptions::read_source`, `make_local_read_source`, "Preserve `Client` constructors and legacy methods" -> present on main (include/detent/client.hh:118; include/detent_local/client_source.hh:32), removed by legacy-h.

- `docs/DEPENDENCIES.md` | **current** | every claim verified against the tree
  - 35 manifest entries, 30 Kronuz plus asio, cppcodec, rapidjson, lz4, zstd (cmake/dependencies.json, counted); `tools/check_pins.py` and CI job `pin-check` (.github/workflows/ci.yml:75-84); `cmake/DependencyPins.cmake` exists.

- `docs/DEPENDENCY_UPGRADES.md` | **stale** | titled "Pending library upgrades" but its top section says the October passes below it are history; one broken link
  - L1 title "Pending library upgrades" vs L3 "describe the October qualification passes against the old pins and are history"; retitle and banner it as HISTORY.
  - L262 link `../measurements/snapshot-pressure-20261029/README.md` resolves to ROOT/measurements (absent); the file is docs/measurements/snapshot-pressure-20261029/README.md.
  - Names removed things as history (classic distributor 11x, `DetentRaftDelegate`, http-log, generation-cache: CHANGELOG.md:29); fine under a record banner, not as "pending".

- `docs/DISK_LIFECYCLE_ARCHITECTURE.md` | **stale** | status plans "the remaining Stage 4 resource component", which is accepted; legacy-lease compatibility text remains
  - L3 "does not accept unimplemented storage lifecycle behavior or complete Stage 4" -> stage-4-distribution landed 2026-10-09; L33 "New readers accept both empty legacy leases and validated managed leases" is per-version recognition (D43). Record formats verified: `DTSTORE1` src/detent_local/ownership.cc:41, `DTLEASE1` :63, `DTIMAGE1` src/detent_local/layout.cc:8, 176-byte descriptor layout.cc:83.

- `docs/DISK_STORE_INTEGRATION.md` | **stale** | "is being implemented" for an accepted component; `UpgradeLegacy` is live code not covered by any quest
  - L121 "The integrated component is being implemented in `disk_store.hh/.cc`" -> exists (include/detent_local/disk_store.hh, src/detent_local/disk_store.cc), accepted per L3.
  - L23, L42, L52 `StoreOpenMode::UpgradeLegacy` exists (include/detent_local/disk_store.hh:9; `--upgrade-managed` src/detent_agent/main.cc:116); no campaign quest names it, a D43 candidate.

- `docs/DISTRIBUTION_ARCHITECTURE.md` | **stale** | status "Whole-component acceptance remains pending", a shared-memory-era `Client::on_update` plan and a live `image_format` paragraph [OWNED legacy-i, legacy-h, dense-pages]
  - L5 "Whole-component acceptance remains pending" -> stage-4-distribution landed 2026-10-09 (state.json). L21, L133 "sibling `cluster/completion/`" -> `completion` is its own library (cmake/dependencies.json:159).
  - L196, L220 "`Client::on_update` ... Retain JSON polling through an explicit legacy adapter" -> shared-memory and legacy polling removed (CHANGELOG.md:46-50).
  - L239 "Managed source configuration accepts `image_format`: `paged-v2` (default) or `flat-v1` ... migrates a configured flat head" is true on main (configuration.cc:141-146) and removed by legacy-i.

- `docs/DISTRIBUTION_CODEC.md` | **current** | dated Stage 4 codec comparison; every figure matches the committed JSON
  - Verified: `benchmarks/distribution-codec-comparison.json` rows for 500,000 knobs (JSON 955-1,205 ms encode, MessagePack 123-130 ms, Protobuf 331-387 ms; raw and zstd3 bytes identical); `DETENT_BENCHMARK_PROTOBUF` at `benchmarks/CMakeLists.txt:56`; target `detent_distribution_codec_bench` at `benchmarks/CMakeLists.txt:52`.

- `docs/DOCUMENTATION_FINDINGS.md` | **stale** | 2026-10-09 record whose open-item lists (sections 3-6) read as live; names removed things and repeats D43-era history
  - L35 "Legacy agent capacity vs. client limits ... fixed 4 MiB mapping" -> the unmanaged shared-memory agent is removed (`docs/status/legacy-wave2.md` decision 7); no shm code in `src/detent_agent/`.
  - L45-52 sections 4-5 list "unimplemented" spec items and binding gaps as open; not re-checked since 2026-10-09 and not labelled as dated; L71 section 7 points maintainers at `website/.../roadmap.md` and `reference/cpp-api/*.md` (Client pages go with legacy-h). OWNED legacy-h

- `docs/EXTRACTION_CHECKLIST.md` | **stale** | "restart index" of 2026-10-27, superseded by the campaign quests (resource-adoption, http-client-adoption, msgpack-adoption, persistent-map-adoption)
  - Names `.scratch/...` worktrees and `track/resource-adoption`, `track/msgpack-adoption` branches (no such local refs in ROOT; `track/persistent-map-adoption` exists), and an "Active extraction step" that is a dated status, not a description.
  - EX-0 claim `Detent:4f1a086` landed on main is true (ancestor of HEAD).

- `docs/FAST_CLIENT_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-25) for prepared attachment; labelled with date and status. OWNED legacy-h, dense-pages
  - L5 times "entry into `Client` construction" and L13 keeps "the paged-v2 data representation": both names change when legacy-h (Client) and dense-pages (DTPAGE03/DTROOT03) land.

- `docs/FLEET_QUALIFICATION_ARCHITECTURE.md` | **current** | dated design (2026-10-15); named files exist
  - Verified: `benchmarks/detent_fleet_load.cc`, `benchmarks/fleet_load.{hh,cc}`, `tests/detent_system/run_fleet.py`, `src/detent_delivery/client_probe_internal.hh:9` (`ProbeAccess`).

- `docs/FLEET_QUALIFICATION.md` | **current** | runner reference; flags and targets verified. OWNED legacy-h
  - Verified: `--active` `--workers` `--http-workers` `--offer-rate` `--mixed` at `tests/detent_system/run_fleet.py:533-544`; CTest `test_system_fleet_${mode}` at `tests/detent_system/CMakeLists.txt:76`; presets under `tests/detent_system/presets/`.
  - `detent_system_local_oracle` and `detent_system_mixed_oracle` link `detent::local_client` (`tests/detent_system/CMakeLists.txt:36,38`); legacy-h ports them off the Client.

- `docs/HOW_IT_WORKS.md` | **stale** | mostly accurate and checked against main, but narrates removed things as history (D43); mirrored by `website/src/content/docs/architecture/how-it-works.md`. OWNED legacy-h, reader-s6, namespace-s3b, namespace-s4-s10
  - L36 "Legacy wave 2 removed the unmanaged agent. It kept one `detent::Client` ... formerly `src/detent_agent/agent.hh`" and L21-22 table rows "Removed (legacy wave 2, step G)" -> history/changelog text; the live fact is only that `detent get` and `detent-agent` exit `no source configured` (`src/detent_agent/main.cc:105,305`). Delete the narration.
  - L34, L115 "Until reader step S6 lands, applications read through `detent::Client` with `make_local_read_source`" and "polls `selected` every 200 ms" -> true on main today (`include/detent_local/client_source.hh:31`, `:10`); both change when legacy-h/reader-s6 land.
  - L28 `change_history.hh:8` and L30 `configuration.hh:8-9` are off by one: the 4,096 / 16 MiB values are at `src/detent_authority/change_history.hh:9`; 64 MiB / 64 edges at `include/detent_relay/configuration.hh:9-10`.
  - Verified true: file sizes (`paged_layout.hh:8` 88/96/80/320; `ownership.hh:8-9` 64/128; `generation.hh:33-34` 176/192; `live_record.hh:14` 1,152/72/32), pack 1 MiB (`paged_layout.hh:7`), compaction 60 s (`coordinator.hh:31`), scrub 524,288 (`coordinator.hh:33`), relay health `scrub_damaged_packs` (`src/detent_relay/host.cc:112`), `test_testing_trusted_restart_oracle` (`tests/detent_testing/CMakeLists.txt:13-14`).

- `docs/HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-19); extraction not landed. OWNED legacy-h
  - L19 "Move the blocking poll/publisher client" and L36 "legacy poll/publisher adapter": that client is the Client's poll source (`src/detent/client.cc`, `include/detent/client.hh:99`), which legacy-h deletes; the quest http-client-adoption then covers two clients, not three.

- `docs/HTTP_LOGGING_ARCHITECTURE.md` | **stale** | says "synchronous classic-distributor metadata logging [is] implemented"; that distributor and its `http_log` pin are gone
  - L5 -> `cmake/dependencies.json` has no `http_log`; `docs/status/legacy-wave2.md:32` removed it "because only the classic access log used it"; no access-log code in `src/`.
  - Proposed `HttpHandler::create_response_observer` is in the external `http` library; nothing in `src/` uses it; authority/relay HTTP logs still absent.

- `docs/IMPLEMENTATION.md` | **stale** | 507-line running ledger with an undated "Status" (Stages 0-4 accepted, Stage 6 active); entries describe the classic path and flat/legacy images as live
  - L41 "The production agent remains on the legacy distribution path", L72 "preserving named legacy compatibility", L47 "dual legacy/managed SDK reader support" -> classic distributor, unmanaged agent and legacy-compat readers are removed or owned by D43. OWNED legacy-i
  - L371 "supported empty legacy anchors" and L473 `image_format` (`flat-v1` by default) -> `legacy_anchor` at `src/detent_local/paged_named_root.hh`; default is now `PagedV2` (`include/detent_delivery/coordinator.hh:30`). OWNED legacy-i, dense-pages

- `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md` | **stale** | Stage 5C blueprint (2026-10-11) for the v2 paged format, presents v1/v2 coexistence as live; HOW_IT_WORKS links it as the format reference. OWNED dense-pages, legacy-i
  - L8 "Whole-image v1 remains supported; paged publication is an explicit configuration choice" -> default writer is `PagedV2` (`include/detent_delivery/coordinator.hh:30`); v1 is being removed (D43).
  - L80 "Keep v1 codecs and publication callable. Upgrade readers before enabling v2 writers" -> compat/migration text; the format is replaced by DTPAGE03/DTROOT03 (`docs/design/dense-pages.md:19`, no legacy reader).

- `docs/LIBRARY_ADOPTION_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-17); status says no pin changed in that pass
  - `.scratch/stage6-btree/after` and `ARM/bin/` are uncommitted scratch paths named as inputs; fine for a record.

- `docs/LIBRARY_BOUNDARIES.md` | **current** | dated recommendation (2026-10-10) with a Progress log, pointing to EXTRACTION_CHECKLIST as the live index; classic Raft and distributor appear as history
  - Dead references: `docs/design/track-c.md`, `detent_transport/tls.hh` (moved to `reactor::tls`, L278 says so); sibling-repo paths (`cluster/...`, `consensus/...`) are other repositories.

- `docs/LINUX_COMPLETION.md` | **current** | `storage_backend` and Linux health fields exist; figures match the JSON
  - Verified: `storage_backend` `"auto"` default at `src/detent_authority/service.hh:39`; `io_native_available`/`io_setup_error`/`io_ring_errors` at `src/detent_authority/service.cc:1571-1573`; medians in `benchmarks/linux-completion-baseline.json:83-99`.

- `docs/LOCAL_IMAGE_FORMAT.md` | **wrong** | titled "version 1" and opens by saying the agent publishes v1 images with an opt-in paged writer; main writes paged v2 by default and the file mixes flat, paged, migration and reader-upgrade history. OWNED legacy-i, dense-pages
  - L3 "publishes dynamically sized immutable v1 images ... an opt-in paged writer ... The legacy agent shared-memory mode remains separate" -> default `LocalImageFormat::PagedV2` (`include/detent_delivery/coordinator.hh:30`); the shm agent is removed. L239 of the same file says `paged-v2 (default)`: self-contradictory.
  - L149, L163 "full native build passes all 60/61 Detent tests ... legacy agent/distributor" -> dated test-run narration of removed targets; L173, L217, L243, L254 "Upgrade readers before enabling", "migrates a configured flat head", "Downgrading" -> compat text (D43).
  - L239 documents `image_format` `paged-v2`/`flat-v1` (`src/detent_delivery/configuration.cc:141-146`): removed by legacy-i. Magic strings on main: `DTIMAGE1`, `DTPAGE02`, `DTROOT02` (`src/detent_local/`); DTPAGE03/DTROOT03 replace them.

- `docs/LOGGING.md` | **wrong** | logging flags and limits are right, but the footprint and HTTP-cost sections measure the removed classic distributor and unmanaged agent, and one example claims a behavior no code has; mirrored by `website/src/content/docs/operations/logging.md`
  - L13 "`detent-agent -vv` includes agent source metric samples, every 200 milliseconds" -> no such sampling in `src/detent_agent/` (managed logs only at `src/detent_agent/managed_host.cc:47-224`); it described the removed unmanaged agent.
  - L46 "The distributor used its default four reactors and eight workers ... The agent used one small defaults file and no source" -> the distributor and no-source agent are gone; `detentd --workers` is a removed option (`src/detent_cli/config.cc:113`). Table L50-52 is verified in `benchmarks/logging-footprint.json` but measures old binaries.
  - L30, L56-68 "HTTP access logging is pending" and the "HTTP metadata and color-depth costs" table (matches `benchmarks/logging-cost-comparison-20261021.json` summary) measure the classic access log, removed with `http_log`; authority/relay never gained one.
  - Verified true: `--log-level` names, `-v`/`--verbosity 0..2`, `--no-color`, `truecolor|24bit|256|256color|16|ansi` (`src/detent_cli/logging.cc:14-51`), `max_pending = 4096`, `iterm2 = false` (`:63-64`); relay logs via the same logger (`src/detent_relay/host.cc:335`). Lines 72-82 are an upstream wish list, not behavior.
  - MISSING: `detent-relay` as a third daemon is named but no relay example; `--quiet` exists (`src/detent_agent/main.cc:394`).

- `docs/MANAGED_FAILOVER_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-14); endpoint pool and refresh are on main
  - Verified: max eight endpoints (`src/detent_delivery/source_selection.hh:15`), `quorum_refresh_ms` 30000 default (`src/detent_delivery/configuration.cc:195-196`). L5 "Legacy single-endpoint managed sources" is wording only: `address`/`port` are live (`configuration.cc:171-174`).

- `docs/MSGPACK_EXTRACTION_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-19), extraction not on main (`cmake/dependencies.json` has no `msgpack`)
  - `detent_distribution/messagepack.hh` named as the private codec is not under that path now; check before the quest lands.

- `docs/ONLINE_CAPACITY_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-23), explicitly "implementation pending"; no capacity-contract code in `src/detent_authority/service.cc`

- `docs/PAGED_DELIVERY_REVIEW.md` | **stale** | 2026-10-14 review proposing a `FlatV1` default; main defaults to `PagedV2`. OWNED legacy-i, dense-pages
  - L25 "`LocalImageFormat { FlatV1, PagedV2 }` and a default `FlatV1` writer preference ... strict `image_format` values `flat-v1` and `paged-v2`" -> `include/detent_delivery/coordinator.hh:17,30` (default `PagedV2`); both names go with legacy-i.

- `docs/PERSISTENT_MAP_EXTRACTION_ARCHITECTURE.md` | **current** | dated plan (2026-10-28), "planning only"; `src/detent_authority/persistent_map.hh` still holds the private AVL. OWNED persistent-map-adoption

- `docs/PERSISTENT_TRANSPORT_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-16); status line "in progress" predates landing
  - Cache exists: `src/detent_delivery/connection_cache.cc`. The EADDRNOTAVAIL narrative matches `benchmarks/fleet-bootstrap-qualification.json` as cited.

- `docs/PLAN.md` | **current** | labelled "Status: HISTORY (2026-11-08)", Dense-pages scope record; not the campaign PLAN (`.campaign/detent/PLAN.md`)

- `docs/PREPARED_ACTIVATION_ARCHITECTURE.md` | **stale** | 2026-10-26 blueprint, status "implementation pending" though prepared reads and the paged default are on main; carries flat-v1 compatibility rules. OWNED legacy-i
  - L9 "Keep explicit `image_format: "flat-v1"` for compatibility ... `reader_validation: "full"` for flat/untrusted compatibility" -> implemented: `coordinator.hh:30` default `PagedV2`, `src/detent_agent/main.cc:90` default `prepared`; compat text is D43 material.

- `docs/PREPARED_PACK_ACCESS_ARCHITECTURE.md` | **current** | dated; extent-only mapping is implemented
  - Verified: `src/detent_local/segment_mapping.hh:32` ("extent-only mapping has no ownership evidence").

- `docs/PRODUCTION_CONTRACT.md` | **stale** | an operating contract that presents flat-v1, upgrade ordering and migration as live deployment rules. OWNED legacy-i, dense-pages, legacy-h
  - L25 "Deploy compatible readers before upgrading writers ... An old paged head without prepared metadata receives an equal-cursor generation at startup ... Flat state migrates" and L27 "Explicit `image_format: "flat-v1"` ... support compatibility deployments" -> D43: one format, refuse and refetch.
  - L13 "Applications read their agent's selected local generation and configured defaults" -> defaults file is legacy-h/`--defaults` (`src/detent_agent/main.cc:235`).
  - Verified: result window 256 (`src/detent_authority/state.hh:29`); 117 MB image ~ `mapped_peak` 117,895,168 (`benchmarks/paged-delivery-measurements.json:45`).

- `docs/READER_CAPACITY_ARCHITECTURE.md` | **stale** | status "real CLI at 1M remains open" and keeps the removed shared-memory path; the settings are implemented
  - L11 "retain identity validation, stopped-daemon reads, defaults and the legacy shared-memory path" -> shm path removed; `reader_*` fields and defaults are at `src/detent_agent/reader_capacity.cc:50-52` (8 MiB, 512 MiB, 16,384). L5 "Keep signed legacy fields unchanged" and L9 "Legacy files without reader settings retain their current behavior" are compat wording.

- `docs/READER_CAPSULE_ARCHITECTURE.md` | **stale** | design for a 624-byte `.read` capsule that is not on main (no `capsule` in `src/`; only `track/reader-capsule`), carrying legacy-writer rollout and rollback rules
  - L38 "A 192-byte record ... a 624-byte record" -> only the 192-byte `.read` exists (`include/detent_local/generation.hh:34`). L56-65 "Deploy compatible SDKs ... legacy-writer mode ... Rollback" -> D43.

- `docs/RECOVERY_RECLAMATION_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-24); narrow state-collector pacing note

- `docs/RELAY_ARCHITECTURE.md` | **current** | dated Stage 5B blueprint (2026-10-10, accepted 2026-10-11); 243 lines, triaged
  - Dead scratch paths only (`.scratch/relay-admission/`, `.scratch/relay-coordinator/footprint-tests.log`). L67 "legacy/sibling profiles" and L164 "legacy distribution request parser" are history wording.

- `docs/RELAY.md` | **missing content** | accurate guide; lacks keys and health fields the code serves; mirrored by `website/src/content/docs/guides/durable-relay.md` and `architecture/relay.md`. OWNED legacy-h
  - Verified: config keys, bundle/credential bounds (256 / 10,000, `src/detent_relay/configuration.cc:100,139`), token 32-128 printable bytes (`:145-148`), `connections` and `service_limits` ranges (`:62-83`), defaults 256 MiB / 64 MiB / 64 MiB / 16 MiB (`include/detent_delivery/configuration.hh:11`, `include/detent_relay/configuration.hh:9,18-19`) and 1,024 / 640 / 512 / 128 (`include/detent_relay/service_limits.hh:6-8`), port 8318 (`configuration.hh:13`), `relay.pid` (`src/detent_relay/main.cc:45`), subscription-only (`configuration.cc:117-120`).
  - MISSING: per-bundle `cache_bytes` (default 64 MiB, min 1 MiB) and `retained_edges` (default 64, 1-256) `src/detent_relay/configuration.cc:103,122-123`; host `control_bytes` (min 16 MiB), `baseline_bytes` (min 1 MiB), `heap_bytes` `:95-98,137`.
  - MISSING: health fields `source_rotations`, `quorum_refresh_failures`, `scrub_passes`, `scrub_damaged_packs`, `sync_files`, `sync_groups`, `quorum_open_age_ms`, `snapshot_ready` `src/detent_relay/host.cc:109-132`.
  - L3 "Relay hosting has Stage 5B component acceptance" is process-status text; L80 "Applications continue using ... local defaults" is the legacy-h defaults file.

- `docs/RELEASE_PLAN.md` | **stale** | 2026-10-27 plan whose RC-1..RC-5 exits predate the campaign's rc1-rc5 quests; carries removed-feature adoption status. OWNED rc1-rc5
  - L41 "classic distributor HTTP metadata adopted" -> distributor and `http_log` removed (`docs/status/legacy-wave2.md:32`).
  - Exits and "Remaining work" (EX-1..EX-4, CL-2) are now defined in `.campaign/detent/quests/`; the table is a second, diverging copy.

- `docs/RESOURCE_EXTRACTION_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-18) with committed evidence (`benchmarks/resource-extraction-qualification-20261019.json`). OWNED resource-adoption

- `docs/RESTORE_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-16) whose status already says the `detent-authority-admin` tool no longer exists. OWNED rc4
  - L18 `src/detent_authority/admin.cc` and `tests/detent_system/run_restore.py` do not exist (status line says a new host is needed).

- `docs/RETAINED_STATE_ARCHITECTURE.md` | **current** | dated blueprint; `retained_state` config exists (`src/detent_authority/service.cc:210,235`)

- `docs/SCALE_TARGETS.md` | **wrong** | requirements doc (2026-10-10, updated 2026-10-22) whose "Today" tables are out of date and name the Client
  - L44 "`authority.json` has no field" for limits -> `application_limits`, `delivery_limits`, `subscription_limits`, `journal_limits`, `retained_state` exist (`src/detent_authority/service.cc:208-212`).
  - L46, L86, L142 `InitOptions::max_knobs` (100,000, 16 MiB, `include/detent/client.hh:103`) and "in-process `detent::Client` 149 ns": true on main, gone with legacy-h; the 149 ns and "5.2 +/- 0.4 ms" have no committed record. OWNED legacy-h
  - L90 "Every update still rebuilds and rewrites the complete image on every agent; incremental pages are Stage 5C" and L150 "A reader currently verifies the image checksum and validates every row" -> paged copy-on-write publication and prepared reads are on main (`docs/HOW_IT_WORKS.md` section 5).

- `docs/SCALING.md` | **wrong** | 2026-10-02 preliminary plan states a removed endpoint and removed components as "current"; superseded by VALIDATED_ARCHITECTURE and SCALE_TARGETS. OWNED legacy-h
  - L54 "The `/v1/snapshot` JSON endpoint remains useful for bootstrap and administration" -> no daemon serves it; only `/v1/health` plus `/v2/*` routes (`src/detentd/`, `src/detent_authority/service.cc`; `website/.../guides/durable-authority.md:15` says so). Named by legacy-h A10 (`docs/SCALING.md:54`).
  - L31 "The current distributor serializes and persists a full snapshot ... The agent has a fixed 4 MiB mutable mapping ... seqlock" -> distributor and shm agent removed (`docs/status/legacy-wave2.md` decisions 1, 7); L58-73 `cluster::Bus`/`cluster::Raft`/"legacy consensus" -> no `cluster` in `cmake/dependencies.json`.
  - Verified: benchmark table L138-139 equals `benchmarks/scaling-baseline.json` (11,123,377 / 9,201,397 / 195.872 / 296.13 / 148.624 / 19.778 ms; 500k row); target `detent_scale_bench` at `benchmarks/CMakeLists.txt:25`. `fits_legacy_4mib_image` is in the JSON.

- `docs/SERVICES.md` | **stale** | operational facts verified, but D43 text and status narration; mirrored by `website/src/content/docs/operations/services.md`. OWNED legacy-h
  - L66 "Lifecycle subcommands are removed" and L84 "Hosting has Stage 5B component acceptance; ... do not establish sustained fleet acceptance" -> changelog/process text (D43).
  - L25 "Readers may use optional `defaults.json` beside the config" -> `--defaults` and `defaults_path` exist on main (`src/detent_agent/config.cc:51,71-84`); legacy-h removes them from `detent get` until reader S7. OWNED legacy-h, reader-s7-s9
  - Verified: directories (`src/detent_cli/config.cc:34-39,73-77,81-87`), pid `0644`/`0600` (`src/detent_cli/daemon.cc:58`), units pass `--system --config` and `StateDirectory=detent-agent` 0755 (`packaging/systemd/detent-agent.service`), relay 8318 and `StateDirectory=detent-relay` 0700 (`packaging/systemd/detent-relay.service`), rc.d and launchd files exist, 180 s startup (`src/detent_agent/config.hh:9`).
  - L88 "approximately 60-second 5M full recovery" has no committed measurement; see UNVERIFIED.

- `docs/SHARED_LIBRARY_DECISIONS.md` | **current** | dated decision record (2026-10-17, "pending Detent's review"); names sibling-repo branches and `.scratch` logs

- `docs/SHARED_MEMORY_PADDING.md` | **delete** | documents only the removed unmanaged agent's shared-memory records
  - `KnobRecord`, `OverrideRecord`, `test_snapshot_canonical` and `src/detent_agent/shm_*` do not exist on main (grep of `src include tests`); legacy-wave2 step G deleted them. Not indexed by `website/.../architecture/design-documents.md`.

- `docs/SNAPSHOT_TRANSFER_ARCHITECTURE.md` | **current** | dated Stage 2 component blueprint, undated header; "older legacy Raft APIs" is history wording

- `docs/STAGE6_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-14); components map to `tests/detent_system/` and `benchmarks/detent_fleet_load.cc`; superseded in scope by rc3-rc5

- `docs/STORAGE_BARRIER_ARCHITECTURE.md` | **current** | dated blueprint (2026-10-25); status "implementation pending" predates landing
  - Landed: `docs/mechanisms/sync-groups.md` (CURRENT 2026-11-09), `sync_files`/`sync_groups` health at `src/detent_relay/host.cc:114-115`.

- `docs/SUBSCRIPTIONS_ARCHITECTURE.md` | **current** | dated Stage 5A blueprint (2026-10-09); `/v2/distribution/session/{open,next,close}` routes exist
  - Verified routes: `src/detent_distribution_http/session_request.cc:84-88`. "legacy callers use `Automatic`" is API history wording.

- `docs/USAGE.md` | **stale** | accurate to main today but the Client scenario and defaults file are being removed, and it carries compat text (D43); mirrored by `website/src/content/docs/guides/shell-and-agent.md`, `managed-delivery.md`, `cpp-application.md`. OWNED legacy-h, legacy-i, reader-s6, reader-s7-s9
  - L38-44, L62-84 Scenario 1 and `read_view()` "Existing file/default clients" -> `detent::Client`, `InitOptions`, `defaults_path`, `source_path`, `source_poll`, `make_local_read_source` all exist on main (`include/detent/client.hh:60,62,99,118`; `include/detent_local/client_source.hh:31`); the whole scenario must be rewritten to `Reader` when legacy-h lands.
  - L140 "`--defaults PATH` supplies baked typed defaults" and L165-171 `defaults.json`/`DETENT_DEFAULTS_PATH` -> exist (`src/detent_agent/main.cc:158,235`, `src/detent_agent/config.cc:51,71-84`); gone with legacy-h until reader S7.
  - L222 "`--upgrade-managed` explicitly adopts a compatible legacy generation directory" -> flag is live (`src/detent_agent/main.cc:116`, `StoreOpenMode::UpgradeLegacy`) but is compat/migration text (D43); no quest owns removing it.
  - L273 "The bounded legacy configuration path keeps its existing symlink behavior" -> real behavior is "untrusted settings may come from a symlink; trusted ones may not" (`src/detent_cli/config.cc:138,156,177`); rephrase without "legacy".
  - L86, L254 status narration ("has Architect acceptance", "shared source contexts remain a separate qualification item") -> process text, not behavior.
  - MISSING: with `--managed` and no `--generation-dir` the store defaults to `<state dir>/agent` (`~/.local/state/detent/agent`, `$XDG_STATE_HOME/detent/agent`, macOS `~/Library/Application Support/Detent/state/agent`, system `/var/lib/detent-agent` or `/var/db/detent-agent`) `src/detent_agent/main.cc:411-412`, `src/detent_cli/config.cc:50-88`; the L135-136 examples rely on it.
  - MISSING: managed.json keys `policy_epoch`, `delivery_mode`, `compaction_interval_ms` (0 or 1,000-86,400,000, default 60,000), `scrub_bytes_per_second` (default 524,288), `reconnect_maximum_ms`, `image_limits` (records, namespaces, wire_bytes, string_bytes, decoded_bytes, image_bytes, pages, segments, overrides, definition_bytes), `image_format` (removed by legacy-i) `src/detent_delivery/configuration.cc:97-130,141-148,238-248`.
  - MISSING: `agent.json` keys `log_level`, `verbosity`, `color`, `runtime_dir` and the 64 KiB config size cap `src/detent_agent/config.cc:48-52`, `src/detent_cli/config.cc:155`; token minimum 32 characters, namespaces at most 4,096 (`configuration.cc:161,173`).
  - Verified true: config directories and XDG rules (`src/detent_cli/config.cc:19-27,73-77`), runtime dir and `0700` (`config.cc:34-47`, `daemon.cc:57`), `agent.pid`/`agent.log` (`main.cc:68,108`), `no source configured` (`main.cc:105,305`), `running (pid N)` status line (`main.cc:128`), sibling `detent-agent` launch (`main.cc:199-200`), all generation/identity/publisher/reader flags (`main.cc:154-167`), reader defaults 8 MiB / 512 MiB / 16,384 / overrides 256 (`reader_capacity.cc:45-52`), `ViewLimits` 500,000 / 4,096 / 256 MiB / 128 MiB (`include/detent_local/view.hh:28-29`), managed defaults 256 MiB heap, 512 MiB mapping, 1 GiB disk, 4,096 entries, 1,024 generations, poll 200, timeout 60,000, `artifact_mode` 384/416/420 (`include/detent_delivery/configuration.hh:11-12`, `configuration.cc:206-224,249-252`), up to 8 endpoints, `quorum_refresh_ms` 30,000 in 1,000-86,400,000 (`source_selection.hh:15`, `configuration.cc:195-196`), env `DETENT_READER_*` (`reader_capacity.cc:25`), `--publisher-user`/`--reader-validation` have no env alias (`main.cc:90,136`).
  - Evaluation order L113-118 (CLI override, switch, rollout, default) agrees with `docs/mechanisms/evaluation.md`; the numeric outputs at L107-110 and L58-59 are in UNVERIFIED.

- `docs/VALIDATED_ARCHITECTURE.md` | **current** | labelled "records the October 2 source review"; findings are history by its own statement
  - Seven relative links point at files that no longer exist: `../src/detent_agent/shm_format.hh`, `shm_reader.cc`, `shm_writer.cc`, `../src/detentd/detentd.cc`, `raft_delegate.hh`, `../../Kronuz/...` siblings. Expected for a record; broken as links.


## Rewrite scope

One section per area the area quests cover. "Pages" are the pages the area owns, "In code, on no page" is what the code serves that no page documents, "Write from" is the source of truth. Pages another not-yet-landed quest changes are marked (see "## Owned by other quests"). Every rewritten page follows D43 (no compatibility, history or version text), D52 (every command and request on a page runs in the docs E2E with an assertion block) and the owner inputs in "## Inputs".

### 1. Architecture


Pages: architecture.md, architecture/{agent, authority, delivery, dependencies, design-documents, how-it-works, read-path, relay, update-path}.md, about.md, benchmarks.md (12). Diagrams checked: all d2 blocks (architecture 1, agent 1, authority 1, delivery 1, dependencies 1, read-path 1, relay 1, update-path 1, how-it-works 7).

In code, on no page (concrete):
- Authority routes and result names: src/detent_authority/service.cc:775-827,1066,1085,153-154; non-loopback `publisher_bind` requires `publisher_tls`: service.cc:420-423; `storage_backend` auto|native|fallback: service.cc:341, docs/LINUX_COMPLETION.md:13.
- `detent-agent` and `detent` flags, `agent.json` keys, env vars, `--status` output: src/detent_agent/main.cc:99-141,154-167,325-358,408-411.
- Relay config keys, limits, flags and health JSON: src/detent_relay/configuration.cc:59-62,99-109,143; main.cc:17-39; host.cc:73-130.
- managed.json keys: src/detent_delivery/configuration.cc:102-131 (cluster, group, epoch, principal, policy_epoch, namespaces, token, address, authority_endpoints, quorum_refresh_ms, port, hostname, ca, heap_bytes, mapping_bytes, artifact_mode, disk_*, poll_interval_ms, reconnect_maximum_ms, delivery_mode, image_format, compaction_interval_ms, scrub_bytes_per_second, timeout_ms, compression, reset_from, image_limits).
- Storage lifecycle on main: trusted restart, first-touch bitmap, scrub, compaction, `change` file: docs/mechanisms/trusted-restart.md, verified-page-bitmap.md, change-file.md; src/detent_local/trusted_restart.cc, pack_scrub.cc.
- Measurements on main beyond the page: docs/perf/*.md, docs/measurements/*, benchmarks/fleet-*.json.
- Packaging: packaging/homebrew/detent.rb, launchd/systemd/rc.d templates; Python and Rust bindings.

Write from (source of truth):
- Binaries and flags: src/detent_agent/main.cc, src/detentd/main.cc, src/detent_relay/main.cc, src/detent_cli/.
- Routes, keys, limits: src/detent_authority/service.cc, src/detent_authority/state.hh, src/detent_relay/configuration.cc, include/detent_relay/configuration.hh, src/detent_delivery/configuration.cc, include/detent_delivery/coordinator.hh.
- Store and pages: include/detent_local/paged_layout.hh, generation.hh, ownership.hh, src/detent_local/live_record.hh, docs/mechanisms/*, docs/design/dense-pages.md (for Dense v3).
- Build and dependencies: CMakeLists.txt, src/CMakeLists.txt, src/detent_local/CMakeLists.txt, cmake/dependencies.json, docs/DEPENDENCIES.md, benchmarks/CMakeLists.txt.
- Numbers: benchmarks/scaling-baseline.json, distribution-codec-comparison.json, application-quanta-baseline.json, docs/perf/*, docs/measurements/*; run the benchmarks to get the rest.

Highest-weight defects:
1. architecture/delivery.md describes the flat v1 image as the generation format; the default is paged (contradicts how-it-works.md).
2. architecture/update-path.md: invalid candidates do not back off and no HTTP source exists (diagram and backoff section wrong).
3. benchmarks.md: the "~150 ns per read, 0.015 core" projection and read-path.md's "149.1 ns/read" use aggregate wall time per op across 4 threads (benchmarks/detent_read_bench.cc:75-76).
4. benchmarks.md Footprint measures the removed unmanaged agent (defaults file, no source); contradicts dependencies.md's size table; dependencies.md's "Default (no CMAKE_BUILD_TYPE)" row describes a build the default no longer produces (CMakeLists.txt:9-11).
5. read-path.md diagram says B-tree; the table is a `flat_map` sorted vector.
6. how-it-works.md carries campaign-status narration and a removed-feature history paragraph (D43) and draws `libdetent_reader`, which is not on main.
7. benchmarks.md "planning target of 50,000 agents" vs docs/SCALE_TARGETS.md:15 (250,000+); "Flat image encode" column has no command that prints it.
8. architecture/dependencies.md: "zstd and gzip" (zstd only), missing flat-map/completion/hashes, "Everything is fetched" (OpenSSL is a system dependency).
9. Numbers on the pages that no committed measurement file carries (read/hash/update, authority tables, 490 tx/s, size figures).

### 2. Features and quick start

Pages: index.mdx, welcome.md, basics.md, installation.md, quickstart.md, settings-and-flags.md, rollouts.md, group-overrides.md, README.md, QUICKSTART.md (10). Internal links on all ten resolve (checked by script against the content tree and `/Detent` base, astro.config.mjs:32). No page carries `<!-- e2e:begin -->` assertion blocks yet (D52, quest docs-e2e not landed); the quest docs-site-start owns that.

In code, on no page (concrete):
1. `detent get` surface: `--config --defaults --runtime-dir --managed --generation-dir --system --publisher-user --reader-validation`, `--generation-cluster/-group/-epoch/-scope`, `--reader-records/-image-bytes/-heap-bytes/-mapping-bytes` (src/detent_agent/main.cc:154-167,312-323); type auto-detection and exit codes (main.cc:179-194,294-300); auto-start of a user agent (main.cc:267-281).
2. `detentd` flags and env: `--port --reactors --system --status --config`, `DETENTD_PORT/REACTORS/RUNTIME_DIR`, config file `detentd.json` fields (src/detentd/main.cc:37-54,91-108); `detentd --status` output (main.cc:143-152).
3. `detent-agent --status` and `--upgrade-managed` (src/detent_agent/main.cc:102,116,125-146).
4. Relay: `detent-relay` and `DETENT_BUILD_RELAY` (CMakeLists.txt:43; src/CMakeLists.txt:94-100).
5. Error categories on reads: Uninitialized, InvalidName, WrongType, ConversionFailed, SourceFailure (include/detent/errors.hh:9-16); `KnobError::what()` format `Category: name (detail)` (src/detent/client.cc:63-72).
6. Authority routes beyond open/transactions: `/v2/close`, `/v2/read`, `/v2/result`, `/v2/distribution`, `/v1/health` (src/detent_authority/service.cc:781,812,827,1085,716); compare-and-set fields `expected_namespace`, `expected_revision`, `absent_namespace_revision` (service.cc:787,799); result statuses `applied no_change conflict unknown_session session_fenced out_of_order result_unavailable identity_conflict capacity exhausted` and HTTP 200/409/429 (service.cc:151-160, 171-182), 400 `invalid_request` (service.cc:1110-1117).
7. Limits on definitions and transactions: 128 operations, 256 KiB request, 16 KiB definition, 256 overrides, 500,000 keys, 4096 namespaces (src/detent_authority/state.hh:27-29).
8. `HealthSnapshot` and `Metrics` (include/detent/client.hh:36-57).
9. Client-level sources and patch payloads: `SourcePayload{bytes,is_patch}`, `poll_interval`, `refresh_alert_after`, `max_knobs`, `on_update` (include/detent/client.hh:60-118).
10. `rollout_pct: null`, duplicate-key refusals, strict top-level fields for snapshots (src/detent/json_schema.cc:115-117,274-278).

Write from (source of truth):
- CLIs: src/detentd/main.cc, src/detent_agent/main.cc, src/detent_agent/config.cc, src/detent_cli/config.cc, src/detent_cli/daemon.cc, src/detent_cli/config.hh.
- Authority config and routes: src/detent_authority/service.cc (load_service_options :183-, routes :775-830), src/detent_authority/state.hh, src/detent_delivery/configuration.cc.
- Evaluation and model: include/detent/detail/evaluation.hh, src/detent/hashing.cc, src/detent/json_schema.cc, include/detent/model.hh, include/detent/validation.hh, docs/mechanisms/evaluation.md.
- Build and install: CMakeLists.txt, src/CMakeLists.txt, cmake/dependencies.json, packaging/, .github/workflows/ci.yml.
- Quick-start fixture: tests/detent_authority/tls_certificates.cmake, tests/detent_authority/tls_ambient.cnf.

Highest-weight defects:
1. basics.md:77-78 and welcome.md:14 describe an HTTP/`GET /v1/snapshot` source that does not exist on main.
2. installation.md:13-18: Homebrew installs the pre-authority revision `323b971` (D22), whose `detentd` and `detent-agent` take different arguments than every page documents.
3. basics.md:63-64 and README.md:48: "new knob names are introduced in baked defaults" is false (json_schema.cc:380-382).
4. installation.md:26: "Apple Clang 17 works" conflicts with packaging/homebrew/detent.rb:36-39 and CMakeLists.txt:38 on macOS before 26.
5. README.md:139,387: schema_version 1 / PATCH described as the wire format; README.md:368: 8 suites (14 registered); README.md:255-263 binary sizes and README.md:352-354 benchmark numbers have no committed source; README.md:212 documents the format switch that legacy-i/dense-pages remove.
6. welcome.md:58-60: copy-as-curl emits `http://` for an authority the tutorial starts with TLS.
7. Every C++ snippet and Client paragraph (index, welcome, basics, settings, rollouts, group-overrides, README) is OWNED legacy-h and must be rewritten when it lands; none of the ten pages has e2e assertion blocks.

### 3. Usage guides and operations

Pages: guides.md; guides/{cpp-application,durable-authority,durable-relay,kill-switches,live-updates,managed-delivery,python-and-rust,shell-and-agent,testing}.md; operations/{authority,logging,security,services,upgrading}.md (15).

In code, on no page:
- Authority: `POST /v2/close`, `GET /v1/health`, `429 producer_busy`, `400 invalid_request`, `404 unknown_endpoint`, HTTP code map 200/409/429, the five non-listed result statuses: src/detent_authority/service.cc:152-155,171-181,716,781,833,856,1114.
- Authority config objects `application_limits`, `subscription_limits`, full `delivery_limits`: service.cc:212-325.
- `detentd` flags `--reactors`, `--runtime-dir`, `--system`, `--config`, env `DETENTD_PORT/REACTORS/RUNTIME_DIR`; `--workers` is removed and refused: src/detentd/main.cc:37-54,89,102-108; src/detent_cli/config.cc:112-125.
- Managed config keys `scrub_bytes_per_second`, `image_limits`, `policy_epoch`, `principal`: src/detent_delivery/configuration.cc:102-130,152,226-247.
- Reader trust/capacity: `publisher_user`, `reader_validation`, `reader_*`, `DETENT_READER_*`, `DETENT_DEFAULTS_PATH`: src/detent_agent/config.cc:55-67; main.cc:235; reader_capacity.hh:7-9.
- `detent-relay` CLI and `relay.json` schema: src/detent_relay/main.cc:17-35; src/detent_relay/configuration.cc:60-122.
- Consumer-side CMake options for the library-only build: CMakeLists.txt:41-48,98-112.
- `detent`/`detent-agent` config search order and system fallback: src/detent_cli/config.cc:93-106.
- Security: no at-rest encryption (D47 #25), reader trust model, unauthenticated health.

Write from:
- Authority: src/detent_authority/service.cc (config loader 183-446, routes 775-840, health 716, results 151-181); src/detent_authority/state.hh; include/detent_distribution/protocol.hh; docs/AUTHORITY_ARCHITECTURE.md; docs/AUTHORITY_TRANSPORT.md; tests/detent_authority/test_node.cc.
- Agent/reader/managed: src/detent_agent/{main,config,reader_capacity}.cc; src/detent_delivery/configuration.cc; include/detent_delivery/coordinator.hh; docs/USAGE.md.
- Services/logging: packaging/**; docs/SERVICES.md; src/detent_cli/{config,daemon,logging}.cc; src/detent_agent/managed_host.cc; benchmarks/logging-footprint.json, logging-cost-comparison-20261021.json.
- Relay: src/detent_relay/{main,configuration}.cc; include/detent_relay/*.hh; docs/RELAY.md.
- C++/bindings: include/detent/{client,global,testing,model}.hh; src/detent/{client,json_schema,overrides}.cc; bindings/python/detent_python.cc; bindings/rust/src/lib.rs.

Highest-weight defects:
1. guides/managed-delivery.md:149,175-179 says `image_format` defaults to `flat-v1` and `paged-v2` is opt-in; the default is `PagedV2` (include/detent_delivery/coordinator.hh:30), and the migration/older-binary text is D43 narration (OWNED legacy-i, dense-pages).
2. operations/logging.md:9-10,23,52 shows log lines and agent source transitions that no code emits (`source connected`, `rejected publish (version)`, `-vv` metric samples); real agent lines are in src/detent_agent/managed_host.cc:47-224.
3. operations/security.md:32 claims every token-bearing config file is single-link checked; only the managed credential file is (src/detent_delivery/configuration.cc:68); it also lacks the D47 #25 no-at-rest-encryption statement and the reader trust model.
4. guides.md:16 promises an HTTP endpoint source that does not exist; the Durable Relay guide is not listed.
5. operations/upgrading.md is wholly D43 compatibility narration: delete.
6. Five of nine guides teach `detent::Client`/`InitOptions`/`detent::global`/`TestKnobs` (exist on main, OWNED legacy-h): cpp-application, kill-switches, live-updates (deleted by legacy-h A14), python-and-rust, testing, plus a section of managed-delivery and shell-and-agent.
7. guides/durable-authority.md and operations/authority.md omit `/v2/close`, `/v1/health`, HTTP status codes, `producer_busy`, `application_limits`, `subscription_limits`, most `delivery_limits`, and the systemd read-only `/etc` trap for `directory`.
8. guides/durable-relay.md:31 has a broken relative link (`managed-delivery.md`); "Stage 5B" appears on guides/durable-relay.md:13 and operations/services.md:138 as internal status wording.
9. guides/python-and-rust.md:83-93 and guides/testing.md:96-100 carry "not yet"/"original specification"/"grew from" history text.

### 4. Configuration and limits

The fragment below also covers `reference/cli*`, `knobs`, `errors` and `observability`, which the quest files place in area 5; the area 5 author takes those from here.

Pages: reference/configuration.md, limits.md, cli.md, cli/detent.md, cli/detentd.md, cli/detent-agent.md, knobs.md, errors.md, observability.md (9). Related, not in this slice: operations/authority.md (says "IP host" and "3 or 5 voters"; code allows 3, 5 or 7, src/detent_authority/service.cc:373-375), guides/managed-delivery.md:149 (also states `image_format` default `flat-v1`; same defect as configuration.md:99, coordinator.hh:30), operations/upgrading.md:25 and guides/managed-delivery.md:72 (`--upgrade-managed`, legacy text).

##### In code, on no page (concrete)

CLI flags, complete list (all four programs):
1. `detent` (reader build, target `detent-reader`, src/CMakeLists.txt:106-111): `get NAME`, `help|--help|-h`; value flags `--hash --switch --default --config --defaults --runtime-dir --managed --generation-dir --publisher-user --reader-validation --generation-cluster --generation-group --generation-epoch --generation-scope` and `--reader-{records,namespaces,overrides,image-bytes,string-bytes,pages,segments,heap-bytes,mapping-bytes,owners}`; boolean `--system` (src/detent_agent/main.cc:154-170, 312-323). Page lacks `--publisher-user`, `--reader-validation`, all ten `--reader-*`.
2. `detent-agent` (src/detent_agent/main.cc:325-361, 391-406): `--managed --generation-dir --initialize-managed --upgrade-managed --generation-cluster|group|epoch|scope --config --runtime-dir --system --daemonize --status --quiet --help|-h --publisher-user --reader-validation --reader-*` plus logging `--log-level -v/-vv --verbosity --color --no-color`. Page lacks `--quiet`, `--publisher-user`, `--reader-validation`, `--reader-*`, the logging flags (page covers them only in cli.md).
3. `detentd` (src/detentd/main.cc:115-130, 223-226): `--authority --daemonize --status --runtime-dir --config --port --reactors --system --help|-h` plus logging flags. Page lists all but `--help|-h`.
4. `detent-relay` (src/detent_relay/main.cc:17-35): `--config --system --daemonize --status --runtime-dir --help|-h` plus logging flags. No page.

Config keys, with type, default and validation:
1. `agent.json` (src/detent_agent/config.cc:46-67; kinds src/detent_cli/config.hh:13): `log_level` string, `verbosity` int >= 0 (<= 2 at parse, src/detent_cli/logging.cc:37), `color` string, `defaults_path` path, `runtime_dir` path, `managed_path` path, `generation_dir` path, `generation_cluster|group|epoch|scope` string (hex, all four or none, requires a generation directory, src/detent_agent/main.cc:423-428), `publisher_user` string or uint64 (trusted file), `reader_validation` string `prepared|full` (default `prepared`, trusted file; src/detent_agent/main.cc:90-93), `reader_records|namespaces|image_bytes|string_bytes|pages|segments|heap_bytes|mapping_bytes|owners` positive uint64 and `reader_overrides` uint64 0-256 (trusted file). Page lists nine of 13 non-reader keys' worth: it omits `publisher_user`, `reader_validation` and ten `reader_*`.
2. `detentd.json` (src/detentd/main.cc:91-99): `log_level`, `verbosity`, `color`, `runtime_dir` (path), `port` int >= 0 (<= 65,535 at use, :68-69; default 8317), `authority` (path), `reactors` int >= 1 (<= 65,535, :133; default 4). Page is complete.
3. `authority.json` (src/detent_authority/service.cc:207-209): `cluster`, `group`, `epoch` (32 hex), `directory`, `credentials` (token 32+ chars, producer, namespaces, principal, policy_epoch, publish; :443-455), `node`, `voters` (id, `host` IP literal, port, certificate_sha256; odd 3-7; :373-399), `peer_tls`, `publisher_tls` (certificate, key, ca), `publisher_bind` (IP, loopback unless TLS, :421-423), `publisher_name` (<= 253, no whitespace, default `localhost`), `storage_backend` (`auto|native|fallback`, non-`auto` Linux only, :341-346), `consensus_limits`, `delivery_limits`, `subscription_limits` (page complete), `application_limits`, `journal_limits`, `retained_state`. Page names only `subscription_limits`.
4. `managed.json` (src/detent_delivery/configuration.cc:102-130): 30 keys. Defaults and ranges: `address` `127.0.0.1` (IP literal), `port` 8317 (1-65535), `authority_endpoints` (1-8 of address/port), `quorum_refresh_ms` 30000 (1000-86400000, pool only), `hostname` `localhost`, `ca` required, `timeout_ms` 60000, `poll_interval_ms` 200, `reconnect_maximum_ms` max(30000, poll), `delivery_mode` `poll|subscription`, `compaction_interval_ms` 60000, `scrub_bytes_per_second` 524288, `heap_bytes` 256 MiB (>= 1 MiB), `mapping_bytes` 512 MiB (>= 4096), `disk_bytes` 1 GiB (1024-1 TiB), `disk_entries` 4096 (4-1,000,000), `disk_generations` 1024 (1-1,000,000), `artifact_mode` 384|416|420 (0600 user, 0644 system), `compression` bool, `reset_from`, `image_limits`, `image_format` (to be removed), `token` 32-128, `principal` <= 128, `policy_epoch` >= 1, `namespaces` <= 4096, `cluster`/`group`/`epoch` 32 hex. Named on configuration.md: `image_format` (wrong default), `compaction_interval_ms`, `reconnect_maximum_ms`, `delivery_mode`, `authority_endpoints`, `quorum_refresh_ms`; others are on guides/managed-delivery.md except `image_limits` and `scrub_bytes_per_second` (how-it-works only) and `policy_epoch` (prose only).
5. `relay.json` (src/detent_relay/configuration.cc:58-60): 13 keys, listed under configuration.md MISSING. Documented only in docs/RELAY.md.

Environment variables: `DETENT_MANAGED_CONFIG`, `DETENT_GENERATION_DIR`, `DETENT_RUNTIME_DIR`, `DETENT_DEFAULTS_PATH` (`detent` only), `DETENT_READER_{RECORDS,NAMESPACES,OVERRIDES,IMAGE_BYTES,STRING_BYTES,PAGES,SEGMENTS,HEAP_BYTES,MAPPING_BYTES,OWNERS}` (src/detent_agent/reader_capacity.cc:25-28), `DETENTD_PORT`, `DETENTD_REACTORS`, `DETENTD_RUNTIME_DIR`, `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `HOME`, `TMPDIR` (via `temp_directory_path`), `NO_COLOR` (logger library, not in Detent source). No env for `publisher_user`, `reader_validation`, generation identity, relay. Removed: `DETENTD_WORKERS` (error "was removed", src/detent_cli/config.cc:112-114, 122-123). `detent-relay` reads no environment.

Pages that name something absent from code: none of the flags, keys or env vars on these pages is missing from code; the defects are wrong values (image_format default, stdout/stderr, runtime "snapshot") and the false opening claim.

Voter addresses (owner ruling D47 #26): accept only IP literals today. Parsing code `auto address = asio::ip::make_address(text(field(voter, "host")))`, src/detent_authority/service.cc:381, then `!address.is_unspecified() && !address.is_multicast()` and endpoint uniqueness, :382-383; `text()` allows 4096 characters but `make_address` throws on a name. Other addresses are IP-only too: `publisher_bind` :421, managed `address` and `authority_endpoints[].address` src/detent_delivery/configuration.cc:176-177, 189, relay `address` src/detent_relay/configuration.cc:89. `hostname`/`publisher_name` are TLS names, not connect targets. The configuration reference cannot document DNS voters until code resolves them; the only quest holding it is rc5 (input, not a criterion).

##### Write from (source of truth)

src/detent_cli/config.cc, config.hh, logging.cc, daemon.cc; src/detent_agent/main.cc, config.cc, reader_capacity.cc, config.hh; src/detentd/main.cc; src/detent_relay/main.cc, configuration.cc, host.cc; src/detent_delivery/configuration.cc, include/detent_delivery/coordinator.hh; src/detent_authority/service.cc (load_service_options :183-460), state.hh; include/detent/errors.hh, validation.hh; include/detent_local/view.hh, paged_layout.hh; include/detent_distribution/protocol.hh; kronuz/consensus pin ab05507; docs/USAGE.md:250-273 (reader settings), docs/RELAY.md, docs/LOGGING.md; docs/design/observability.md for the OWNED observability section only.

##### Highest-weight defects

1. configuration.md:99 states `image_format` default `flat-v1`; the code default is `paged-v2` (include/detent_delivery/coordinator.hh:30). The same wrong default is on guides/managed-delivery.md:149. The key itself is deleted by legacy-i.
2. configuration.md:5 "nothing needs configuring" is false (src/detent_agent/main.cc:104-105, src/detentd/main.cc:136).
3. cli/detent.md:57 puts `ERR ...` on stdout; it is stderr (src/detent_agent/main.cc:298), and `--default` is not applied to `SourceFailure` (:294).
4. Voter addresses: IP literals only (src/detent_authority/service.cc:381); D47 #26 DNS names is not implemented and no criterion carries it; the configuration reference must not claim DNS until it is.
5. Reader and publisher settings (`publisher_user`, `reader_validation`, ten `reader_*` keys, `--reader-*` flags, `DETENT_READER_*`) are served by code and documented only in docs/USAGE.md:250-273; no site page names them.
6. `detent-relay` has a CLI, `relay.json` and `/v1/health` and no reference page; cli.md says "three programs".
7. errors.md lacks `SourceFailure` and 14 HTTP error names present in code and on no page (`admission_full`, `scope_forbidden`, `unauthorized`, `invalid_request`, `distribution_unavailable`, `body_bound`, `unknown_endpoint`, `source_not_leader`, `source_behind`, `publish_forbidden`, `producer_busy`, `not_found`, `internal_error`, `authorization_timeout`; `stopping` only on quickstart.md).
8. detent-agent.md `--upgrade-managed` is a legacy-adoption mode that D43 forbids and no quest removes (owner call).
9. observability.md named an internal spec and its metric prefix (now the `Metrics` counters); the Client half goes with legacy-h; the OWNED observability design (usage JSON, textfile) is not on the page and must be tagged when written.

### 5. Reference and the rest

Three fragments: the API, bindings, evaluation, HTTP and format pages; the small pages and `docs/*.md` A to D; `docs/*.md` D to V.


#### 5a. API, bindings, evaluation, HTTP and snapshot format

Pages: reference/cpp-api.md, cpp-api/{client,global-and-testing,sources}.md, bindings.md, bindings/{python,rust}.md, evaluation.md, http-api.md, http-api/{authority,health}.md, snapshot-format.md (12).

In code, on no page:
- Relay HTTP surface (`POST /v2/distribution/session/{open,next,close}`, `GET /v1/health`, 404/403/503 `source_behind`): src/detent_relay/service.cc:106-157,359-414. No `reference/http-api/` page; architecture/relay.md and guides/durable-relay.md may cover it (outside this slice).
- Authority response envelope and status-code map: src/detent_authority/service.cc:151-180,988-994,1124.
- Public headers `json_schema.hh`, `json_patch.hh`, `hashing.hh`, `cursor.hh` (parse/apply/diff, `stable_bucket`): include/detent/.
- `ClientSourceOptions` fields, `make_managed_read_source`, `LocalReadSource::refresh()`: include/detent_local/client_source.hh:6-35.
- Health document fields beyond the core set (connections, requests, reserved bytes, export, peer, checkpoint): src/detent_authority/service.cc:1581-1656.
- Config-overridable limits (`application_limits`, `subscription_limits`, `delivery_limits`): src/detent_authority/service.cc:212-330.

Write from (source of truth):
- C++ API (until legacy-h lands): include/detent/{client,read_view,global,testing,overrides,model,errors,validation,json_schema,json_patch}.hh, include/detent_local/client_source.hh, src/detent/client.cc. After legacy-h/reader-s6: include/detent/reader.h and reader.hh, docs/mechanisms/reader-core.md (not on main today).
- Bindings: bindings/python/detent_python.cc, bindings/rust/src/lib.rs + shim.cc (until legacy-h; then the C ABI and the new modules).
- Evaluation: include/detent/detail/evaluation.hh, src/detent_local/evaluation.hh, src/detent/hashing.cc, docs/mechanisms/evaluation.md, tests/detent/test_hash_golden.cc.
- HTTP: src/detent_authority/service.cc, state.cc, state.hh, read_reply.cc, src/detent_distribution_http/{session_request,reply_pump}.cc, src/detent_relay/service.cc.
- Format: src/detent/json_schema.cc, json_patch.cc, include/detent_distribution/{protocol,framing}.hh, src/detent_distribution/framing.cc; images: docs/LOCAL_IMAGE_FORMAT.md and src/detent_local/layout.cc, paged_layout.cc (check against code, not the doc).

Highest-weight defects:
1. authority.md outcome table: `identity_conflict` is the changed-retry outcome, `conflict` is only a failed precondition, `invalid` is not a status; HTTP status per outcome (200/429/409) and the response body are undocumented (state.cc:930-933, service.cc:151-180).
2. http-api.md/authority.md: "every /v2 request names cluster, group, epoch" is false for session next/close (session_request.cc:160-170); error bodies carry an `authority` envelope; the 400/401/403/404/413/429/503 error set is missing.
3. snapshot-format.md: "an HTTP endpoint returns [a patch] as a diff" describes a route that does not exist; a patch cannot carry `published_at`.
4. client.md: `refresh_alert_after` is a dead option (client.hh:102); `Metrics` are per-Client.
5. D43 history text: sources.md:28-31 and global-and-testing.md:99-103 ("The specification Detent grew from..."), authority.md:5-7,193 ("replaces", "existing ... retains").
6. Relay HTTP routes have no reference page.

#### 5b. FAQ, help, next, roadmap, contributing, copyright and docs/*.md (ACTORS to DISTRIBUTION_ARCHITECTURE)

Pages: site `faq.md`, `help.md`, `next.md`, `roadmap.md`, `contributing.md`, `copyright.md`; top-level guides `docs/ACTORS.md` through `docs/DISTRIBUTION_ARCHITECTURE.md` (30 files, alphabetical; `DISTRIBUTION_CODEC.md` is after the range and not covered).

In code, on no page of this slice:
1. Managed `delivery_mode` poll/subscription and `reconnect_maximum_ms`: src/detent_delivery/configuration.cc:132-140, include/detent_delivery/coordinator.hh:20 (the FAQ's freshness answer assumes polling).
2. Trusted restart and the paced scrub (`scrub_bytes_per_second`, `generation-<id>.live`): configuration.cc:152, src/detent_local/trusted_restart.cc, docs/LOCAL_IMAGE_FORMAT.md:258 (not in ACTORS, FAQ or roadmap).
3. CI gates and build options a contributor must satisfy: .github/workflows/ci.yml:22, :52-73, :75-84, :86-100; CMakeLists.txt:43-50; tools/check_pins.py.
4. Third-party dependency licences: cmake/dependencies.json:3-6,33,93,183,189 (five non-Kronuz libraries).
5. Campaign-scheduled work absent from the roadmap: Dense v3 pages, `libdetent_reader` and the C ABI bindings, namespace delivery S4-S10, observability (docs/design/observability.md), coroutine drivers, RC-1 to RC-5 exits (PLAN.md Goals).
6. Python/Rust binding gaps the reference pages list (managed generation source, `get_as`, panicking helpers) but the roadmap does not: website/src/content/docs/reference/bindings/python.md:73, rust.md:69-70.

Write from:
- faq/next: include/detent/client.hh, src/detent_delivery/configuration.cc, src/detent_agent/main.cc, src/detent/hashing.cc:7-18, include/detent/detail/evaluation.hh:16-20, src/detent_authority/service.cc:207-423.
- roadmap: .campaign/detent/PLAN.md (Goals, D43, D45, D47), state.json quest states, CHANGELOG.md, .github/workflows/ci.yml, docs/SCALE_TARGETS.md, docs/RELEASE_PLAN.md, docs/STAGE6_ARCHITECTURE.md.
- contributing: CMakeLists.txt:41-50, .github/workflows/*.yml, website/package.json, website/README.md.
- copyright: LICENSE, cmake/dependencies.json.
- docs/*.md: CHANGELOG.md (legacy waves, M1), cmake/dependencies.json, src/detent_authority/, src/detent_local/, docs/LOCAL_IMAGE_FORMAT.md.

Highest-weight defects:
1. `faq.md` L37 and `next.md` L33 claim a C++ HTTP source / "HTTP endpoint" that does not exist (include/detent/client.hh:62, :99).
2. `faq.md` L64-67 names a `cluster` library Detent no longer depends on (cmake/dependencies.json:177; CHANGELOG.md:29).
3. `roadmap.md` lists CI (ci.yml:20-26) and a change hook (client.hh:115) as missing, keeps "Legacy Raft ... Xapiand compatibility retained" (L32), a superseded workload (L39-41) and the superseded 500k-observer-deadline narration (L106, D45).
4. `docs/AUDIT.md`, `DELIVERY_INTEGRATION_ARCHITECTURE.md`, `DISTRIBUTION_ARCHITECTURE.md`, `CAPACITY_QUALIFICATION.md` present the removed shared-memory agent, distributor or legacy modes as live; none of the 29 non-ACTORS guides has a HISTORY banner, so a single banner on dated records would settle most of the "stale" rows without rewrites.
5. `docs/AUTHORITY_TRANSPORT.md` L5/L31 names a directory and a test that do not carry the content; `docs/DEPENDENCY_UPGRADES.md` L262 broken link.
6. `contributing.md` gives a build that skips the relay and none of the CI gates (pin-check, source-checks) or the D52 docs check.

#### 5c. docs/*.md (DISTRIBUTION_CODEC to VALIDATED_ARCHITECTURE)

Pages: 46 top-level guides, `docs/DISTRIBUTION_CODEC.md` through `docs/VALIDATED_ARCHITECTURE.md`. Six are user guides with a site counterpart (full claim check): USAGE (guides/shell-and-agent, managed-delivery, cpp-application), SERVICES (operations/services), LOGGING (operations/logging), RELAY (guides/durable-relay, architecture/relay), HOW_IT_WORKS (architecture/how-it-works), SCALING (indexed by architecture/design-documents). The other 40 are dated design/review records, triaged only (head, whole-file grep for removed names, path existence); "current" for them means labelled as a record and no live-sounding claim found wrong, not that every sentence was re-proved.

In code, on no page here (concrete):
- Default agent store path when only `--managed` is given: `src/detent_agent/main.cc:411-412`, `src/detent_cli/config.cc:50-88`.
- Relay per-bundle `cache_bytes`/`retained_edges` and host `control_bytes`/`baseline_bytes`/`heap_bytes`: `src/detent_relay/configuration.cc:95-98,103,122-123,137`; relay health sync/scrub/rotation fields: `src/detent_relay/host.cc:109-132`.
- managed.json `image_limits`, `compaction_interval_ms`, `scrub_bytes_per_second`, `reconnect_maximum_ms`, `policy_epoch`: `src/detent_delivery/configuration.cc:97-130`.
- Agent/detentd config keys `log_level`, `verbosity`, `color` and the 64 KiB config cap: `src/detent_cli/config.cc:155`, `src/detentd/main.cc:92-98`; `detentd` keys `port`, `reactors`, `authority`, `runtime_dir`.

Write from (source of truth):
- CLIs and config: `src/detent_agent/main.cc`, `config.cc`, `reader_capacity.cc`; `src/detent_cli/config.cc`, `logging.cc`, `daemon.cc`; `src/detentd/main.cc`; `src/detent_relay/main.cc`, `configuration.cc`, `host.cc`.
- Managed config and defaults: `src/detent_delivery/configuration.cc`, `include/detent_delivery/configuration.hh`, `coordinator.hh`.
- Store and format: `include/detent_local/paged_layout.hh`, `ownership.hh`, `generation.hh`, `src/detent_local/live_record.hh`, `docs/mechanisms/*.md`, `docs/design/dense-pages.md`.
- Packaging: `packaging/{systemd,launchd,rc.d}/`.
- Numbers: `benchmarks/*.json` only.

Highest-weight defects:
1. `docs/SCALING.md:54` states `/v1/snapshot` remains served; no daemon serves it (named by legacy-h A10). L31 presents the distributor and the 4 MiB shm agent as current.
2. `docs/LOGGING.md:13,46,56-68`: agent "-vv" metric samples, distributor footprint and HTTP-access-log cost describe removed components.
3. `docs/LOCAL_IMAGE_FORMAT.md:3` says v1 images with an opt-in paged writer; default is `PagedV2`; whole file carries migration/upgrade/legacy text that legacy-i and dense-pages retire.
4. `docs/SCALE_TARGETS.md:44` "`authority.json` has no field" for limits; five limit stanzas exist (`service.cc:208-212`).
5. `docs/USAGE.md`: Scenario 1 and `defaults.json` are the Client/defaults surface owned by legacy-h; `--upgrade-managed` "legacy generation directory" (L222) is compat text with no owning quest; missing default store path and managed.json keys.
6. `docs/HOW_IT_WORKS.md:36` and `docs/SERVICES.md:66` carry removal narration (D43).
7. `docs/PRODUCTION_CONTRACT.md:25,27` states flat-v1/migration/upgrade-order as operating rules.
8. `docs/SHARED_MEMORY_PADDING.md` has nothing left to describe: delete.

## Unverified

Claims the audit could not settle from code, each with the probe. A figure counts as verified only when a committed measurement file carries it. Probes run against a fresh build of `main`; `quiet` is the machine's hold wrapper and `campaign job submit` queues a probe as a job.


- `benchmarks.md:L18` | `detent_read_bench 4 2000000` -> 6.71M ops/s (149.1 ns/op) | probe: `quiet exclusive NAME 10 "read bench" -- build/benchmarks/detent_read_bench 4 2000000` on the stated hardware; also `... 1 2000000` to get the per-thread cost
- `benchmarks.md:L19` | `detent_hash_bench 5000000` -> 5.05M calls/s (198.2 ns) | probe: `quiet strict NAME 5 "hash bench" -- build/benchmarks/detent_hash_bench 5000000`
- `benchmarks.md:L38`, `update-path.md:L84` | 28.6K updates/s, 35.0 us/update for 100 knobs | probe: `quiet strict NAME 5 "update bench" -- build/benchmarks/detent_update_bench 200000 100`
- `benchmarks.md:L21` | "one 14-core arm64 development machine" | probe: `sysctl hw.ncpu hw.model; uname -m` on the host that produced README.md:352-354
- `benchmarks.md:L58-62`, `authority.md:L38-41` | 133.708/668.599 ms whole-snapshot patch, 0.0127/0.0113 ms authority update, 26/27 nodes, 0.0141/0.0114 ms CPU | probe: `build/benchmarks/detent_authority_bench 100000 uniform 2` and `... 500000 uniform 2` (only docs/AUTHORITY_ARCHITECTURE.md:112-113 carries the figures)
- `benchmarks.md:L63-66`, `authority.md:L101-104` | about 490 tx/s, 0.042 cores at 500,000 knobs, 128-131 ms group wait | probe: `build/benchmarks/detent_authority_durable_bench .scratch/authority-500k 500000 64` under `quiet exclusive`
- `benchmarks.md:L82-85` | detent-agent 475,520 B stripped, 1,316 KiB RSS, 2 threads; `detent` 257,864 B | probe: build Release with defaults (managed agent ON), `strip -x`, then `ps -o rss=` five times one second after `detent-agent --managed managed.json --generation-dir DIR` starts; the committed file measured the removed unmanaged agent
- `dependencies.md:L76-80` | 1.15 MB / 295 KB / 242 KB sizes | probe: build `detent-agent` three ways (`-DCMAKE_BUILD_TYPE=` empty is no longer reachable; use `None`, `Release`, `Release` + `strip -x`) on macOS/arm64 and `ls -l`
- `dependencies.md:L82` | "Most of the remaining size is RapidJSON and ordinary C++ runtime support" | probe: `bloaty` or `nm --size-sort` on the stripped Release `detent-agent`
- `dependencies.md:L5` | "most of them extracted from Xapiand" | probe: compare the 30 Kronuz repos' histories against Kronuz/Xapiand; no file in ROOT records it
- `authority.md:L23` | grouping "up to 64 producers, 5 ms" | probe: read the pinned Kronuz/consensus Limits/Timing (cmake/dependencies.json `consensus` revision) or `grep -rn "64\|5ms" docs/ACTORS.md:59`; the code here sets `consensus` limits via the library (src/detent_authority/node.hh:26)
- `authority.md:L110-111` | "quanta of at most 8 commands, 128 mutations or 64 KiB" | probe: src/detent_authority/state.hh:74-76 defines `CommandWorkBudget{bytes=8192, records=16, updates=8, receipts=32}`, a different set; run `test_authority_*` quanta case or read docs/BOUNDED_APPLICATION.md to map the numbers
- `authority.md:L52-54` | captures "take microseconds" | probe: docs/AUTHORITY_ARCHITECTURE.md:119 only (7.226-11.693 us); run `detent_authority_bench`
- `authority.md:L135-139` | passed native, ASan/UBSan and TSan on macOS, Linux and FreeBSD; three real HTTPS processes through leader SIGKILL | probe: CI run of the current head (`.github/workflows/ci.yml:25` has `freebsd: false`); the process-kill fixture `tests/detent_system`
- `authority.md:L68` | "no wall-clock expiry anywhere in the protocol" | probe: `grep -rn "steady_clock\|system_clock" src/detent_authority/` and the consensus library's timers
- `architecture.md:L8-9` | a read stays "a few hundred nanoseconds while distribution can be down for an hour" | probe: `detent_read_bench 1 2000000` for the read; a test that stops the authority for 1 hour while an agent keeps serving (tests/detent_system)
- `read-path.md:L86-87` | clean under ASan, UBSan and TSan "on Linux" | probe: CI `asan` and `tsan` jobs of the shared Kronuz/cpp-libraries workflow on ubuntu runners
- `how-it-works.md:L476-477` | "zero pack bytes hashed at startup" asserted by `test_testing_trusted_restart_oracle` | probe: `ctest -R test_testing_trusted_restart_oracle -V` and read the assertion in tests/detent_testing/test_trusted_restart.cc:164
- `how-it-works.md:L64-65` | the authority keeps "the last 4,096 transactions, up to 16 MiB" regardless of configuration | probe: src/detent_authority/change_history.hh:9 shows defaults; `grep -rn "HistoryOptions" src/` for config wiring
- `relay.md:L68-69` | macOS bundle parent `/var/db/detent-relay` | probe: no launchd template creates it (packaging/launchd/org.detent.detent-relay.plist); docs/RELAY.md:114 recommends it
- `about.md:L37` | FreeBSD supported | probe: run the shared workflow with `freebsd: true` (.github/workflows/ci.yml:25)
- `installation.md:L26` | Apple Clang 17 configures and builds without `-fexperimental-library` on macOS before 26 | probe: on a macOS 15 host with Apple Clang 17, `cmake -B build-x -DDETENT_BUILD_TESTS=OFF` and read `DETENT_HAS_JTHREAD`; then repeat with `-DCMAKE_CXX_FLAGS=-fexperimental-library`.
- `installation.md:L34` | "qualified on FreeBSD" | probe: a FreeBSD 14.4 run of `ctest --test-dir build` on current main (CI has `freebsd: false`, .github/workflows/ci.yml:25; last record docs/AUTHORITY_READINESS.md:39 predates main).
- `installation.md:L90-95` | `-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF` configures, builds and installs | probe: `cmake -B b -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF && cmake --build b -j4` on main, under `quiet light`.
- `installation.md:L13` | the published tap `Kronuz/homebrew-tap` serves the formula in packaging/homebrew/detent.rb | probe: `brew info Kronuz/tap/detent` and diff its `revision` against packaging/homebrew/detent.rb:15.
- `index.mdx:L21` | no lock, no allocation per read; "no disk" | probe: `detent_read_bench` under a malloc-counting preload for the Client path; for the mapped path the first-touch cost is the docs/design/reader.md:43 row (measure with `reader_bench` cold vs warm).
- `README.md:L352-354` | `detent_read_bench` 6.71M ops/s (149.1 ns/op), `detent_hash_bench` 198.2 ns/call, `detent_update_bench` 35.0 us/update on a 14-core arm64 machine | probe: `quiet exclusive` run of `detent_read_bench 4 2000000`, `detent_hash_bench 5000000`, `detent_update_bench 200000 100` on the arm64 host, committed under docs/perf/. Only docs/SCALE_TARGETS.md:142 (149 ns, not a measurement dir) and docs/perf/m1-w5-evidence.md:75-78 (260-431 ns/op, Intel i9) exist.
- `README.md:L259-263` | detent-agent 1.15 MB / 295 KB / 242 KB (macOS/arm64) | probe: build the agent Release on arm64 and `ls -l`, `strip -x`; commit the sizes. Only a CMakeLists.txt:5-8 comment carries the 1.15 MB and 242 KB figures.
- `README.md:L370` | ASan/UBSan and TSan clean across the full suite | probe: latest green `linux-asan-ubsan` and `linux-tsan` jobs of `ci.yml` on main's head.
- `README.md:L175` | the 1.1.0 release's `GenericStringRef` assignment fails with Clang 23 | probe: compile a TU with `rapidjson` v1.1.0 headers and Clang 23.
- `README.md:L9`, `index.mdx:L21` | the shared-memory/mapped path allocates nothing and takes no lock on a warm lookup | probe: docs/design/reader.md:124 D3 test; run `reader_bench` with the allocation counter.
- `quickstart.md:L183`, `QUICKSTART.md:L135` | one second is enough for the agent to deliver a publish before `detent get` | probe: run the page under the docs-e2e harness 20 times and count reads that print `ERR ... (no durable local generation available)` or a stale value.
- `quickstart.md:L109-115`, `QUICKSTART.md:L73-79` | the transcript shows exactly two 503 `outcome_unknown` replies before `applied` | probe: `detentd --daemonize` then the `until api open` loop, counting 503s (timing, about 2 s per docs/status/quickstart-managed.md).
- `QUICKSTART.md:L229` | `durable revision=8 knob_count=2` after the recipe | probe: run the recipe through the Stop step and compare (derived above from 8 transactions, not run).
- `guides/durable-authority.md:L12-18` | "passed native, sanitizer and three-platform qualification" | probe: `grep -n "three-platform\|three platform" docs/AUTHORITY_READINESS.md docs/status/*.md`; a committed status file carrying the three-platform result, or the CI run list for the authority suites.
- `operations/authority.md:L41` | retained_state defaults (1 MiB, 64 MiB, 65,536, 2 MiB, 256) and `consensus_limits` 64-65,536 / 2-64 MiB come from the external consensus library | probe: `build/tests/detent_authority/test_node` printing `kronuz::consensus::Limits{}` and `WorkerLimits{}` defaults (only the sum 336,931,316 is pinned by tests/detent_authority/test_node.cc:106).
- `operations/authority.md:L92`, `operations/security.md:L22-24` | TLS below 1.2, early data and renegotiation disabled; voter pin on the DER leaf | probe: start `detentd` with `publisher_tls`, then `openssl s_client -connect 127.0.0.1:8317 -tls1_1` (must fail) and a renegotiation attempt (`-reconnect`/`R`); code is in the external reactor library.
- `operations/authority.md:L66` | token rotation with the same producer name preserves the session; changing the restore identity fences old requests | probe: authority test that rotates a credential token across a restart and re-reads `/v2/result` for the old sequence.
- `operations/authority.md:L108-111` | "Process-kill tests trace every acknowledged result across restarts" | probe: `ctest -R 'authority.*(kill|recover|restart)' --output-on-failure`.
- `operations/logging.md:L32` | `--color auto` honors `NO_COLOR` | probe: `NO_COLOR=1 script -q /dev/null detentd --authority a.json --color auto 2>&1 | cat -v` (escape bytes absent) vs without `NO_COLOR` (present); logic is in the external logger library.
- `operations/logging.md:L56` | HTTP logging omits headers, bodies, query values; labels bounded and escaped | probe: run `detentd -vv`, send `POST /v2/open` with a bearer token and body, capture stderr; expected: no request/response lines at all (no call site in src) or lines free of the token and body.
- `operations/logging.md:L69` | warnings and errors bypass the 4,096 queue and critical messages are written synchronously | probe: read the pinned `logger` revision in cmake/DependencyPins.cmake; or a test that floods 10,000 info lines and asserts a warning is not dropped.
- `operations/security.md:L13-14` | "Readers map the snapshot read-only and bounds-check every offset and length" | probe: `ctest -R 'reader.*(fuzz|capsule|corrupt)'` and the committed docs/measurements/reader-capsule-fuzz-20261029 results.
- `operations/security.md:L39-43` | advisories URL `https://github.com/Kronuz/Detent/security/advisories/new` | probe: `gh api repos/Kronuz/Detent/security-advisories -i` or open the URL signed in (private reporting enabled).
- `guides/cpp-application.md:L138` and `guides/managed-delivery.md:L131` | a view stays valid after the client is destroyed (local-source clients) | probe: a test that takes `client.read_view()` from a `make_local_read_source` client, destroys the client, then calls `view.get(...)` under ASan.
- `guides/live-updates.md:L97-100` | typical poll intervals (200 ms-1 s kill switches, 1-5 s settings) | probe: advice with no code claim; settle with a committed measurement of staleness vs `poll_interval` (none under docs/perf or benchmarks/).
- `guides/kill-switches.md:L69` | "within one poll interval" for every process | probe: end-to-end with authority + `detent-agent --managed` + `detent get` timing a revert (`/v2/transactions` then poll `detent get`), for `poll` and `subscription` delivery modes.
- `guides/durable-relay.md:L31` | agents consuming a relay "must explicitly set `delivery_mode: subscription`" | probe: point an agent with default (poll) delivery at a relay endpoint and read its failure; only docs/RELAY.md states it, no agent-side check found (`delivery_mode_explicit` is read only by src/detent_relay/configuration.cc:117).
- `guides/managed-delivery.md:L173` | "Subscriptions and relay fan-out have component acceptance" | probe: `grep -n "acceptance" docs/status/*.md docs/RELAY_ARCHITECTURE.md`; cite the committed acceptance record or drop the sentence.
- `guides/python-and-rust.md:L94` | no wheel on PyPI and no crate on crates.io | probe: `pip index versions detent` and `cargo search detent-rust`.
- `guides/shell-and-agent.md:L48` (first `get` waits for the first generation) | wait duration | probe: `detent get X --managed m.json` against an authority with data, timing it; bounded by `managed_startup_timeout` 180 s (src/detent_agent/config.hh:9).
- All pages | any `docs-site-audit` E2E execution of the page samples (D52) | probe: the `docs-e2e` extractor against a fresh build; none of the guide commands was executed in this audit (read-only).
- `reference/cli/detent.md:L64-66` | error detail text `(not present in current snapshot)` for a missing knob | probe: build, publish a generation, run `detent get checkout:nope --generation-dir <dir>` and read stderr; code predicts `(local image lookup failed)` (src/detent_local/view.cc:27), the sample text is the Client path (src/detent/client.cc:189)
- `reference/cli.md:L42` and `reference/configuration.md:L145` | `auto` honors `NO_COLOR` and redirection | probe: `NO_COLOR=1 script -q /dev/null detentd --authority <cfg> -v` versus without `NO_COLOR` and read the escape bytes; the logic is in the external Kronuz/logger, not Detent source (docs/LOGGING.md:21 states it)
- `reference/configuration.md:L171-173` | rotation and fail-closed rules for pooled endpoints, "idle responses do not postpone" refresh, "default 60-second request timeout provides no short failover guarantee" | probe: `ctest -R 'test_coordinator|managed_config'` (tests/detent_delivery/test_coordinator.cc) and the failover suite in tests/detent_system/managed.py; only bounds (8, ±20%, 1000-86400000) were read in code (src/detent_delivery/source_selection.hh:15, 56; configuration.cc:195-196)
- `reference/configuration.md:L175` | "An agent consuming a relay must use a single relay endpoint and explicit `delivery_mode: subscription`" | probe: run a managed agent against a relay with `delivery_mode` omitted and with `authority_endpoints`; no client-side check exists (grep `delivery_mode_explicit` hits only the relay loader, src/detent_relay/configuration.cc:117)
- `reference/configuration.md:L131` | "authenticated idle responses and durable publication reset backoff" | probe: unit test in tests/detent_delivery/test_coordinator.cc for `ReconnectBackoff::reset` call sites (include/detent_delivery/reconnect_backoff.hh:16)
- `reference/configuration.md:L101` | compaction "Checks run between upstream exchanges, and admission pressure skips an optional attempt" | probe: `git grep -n compaction_interval src/detent_delivery/coordinator.cc` and run the compaction tests
- `reference/configuration.md:L99` | "Changing to `flat-v1` leaves an unchanged paged selection in place until a forward update publishes a flat image" | probe: moot after legacy-i; today a coordinator test with `image_format` flipped across a restart (src/detent_delivery/coordinator.cc:95, 1160)
- `reference/observability.md:L71-75` | "knob values are never logged" | probe: `git grep -nE 'L_(INFO|NOTICE|WARNING|ERR|DEBUG)' src` and read each call's arguments; the 13 hits seen carry revisions, counts and endpoints
- `reference/errors.md:L57` | `Transport` "or a cluster had no quorum decision in time" | probe: `git grep -n "Transport" src/detent src/detent_delivery` for the reject-reason mapping
- `reference/limits.md:L23` and L50-54 | numbers as measured limits | probe: none needed; these are configured constants, no figure on these pages is a benchmark. No figure was found in docs/perf, docs/measurements, docs/status or benchmarks for them, so they are citations of constants only
- `reference/http-api.md:L26` | oversized request body (> 256 KiB) is refused and with what status/connection behavior | probe: start an authority, `curl -v -X POST --data-binary @<(head -c 300000 /dev/zero) -H 'Authorization: Bearer TOKEN' http://localhost:8317/v2/open`; docs/status/http-coroutines.md:92 says no test covers the authority cap, and docs/design/http-coroutines.md:437 says 413 text/plain with close.
- `reference/http-api.md:L28-29` | authentication and body-admission failures close the connection | probe: `curl -v -X POST http://localhost:8317/v2/open -d '{}'` (no token) and check for `Connection: close` / socket close after the 401.
- `reference/http-api/health.md` (and authority health) | health body begins with the `authority` envelope | probe: `curl -s http://localhost:8317/v1/health | head -c 200`.
- `reference/http-api.md:L5` | HTTP/1.1 only (no h2 negotiation over publisher TLS) | probe: `curl -v --http2 --cacert CA https://HOST:8317/v1/health` and read the ALPN result.
- `reference/http-api/authority.md:L191` | "Next requires exact publication feedback ... duplicate feedback is idempotent" | probe: `ctest -R test_subscription -V` (tests/detent_authority/test_subscription.cc) and read the duplicate-published case.
- `reference/http-api/authority.md:L176-177` | delta/progress sent only when retained history exactly covers `base`, else snapshot | probe: `ctest -R 'test_delta_export|test_distribution_service' -V`.
- `reference/cpp-api/client.md:L104-105` | a managed-generation `ClientReadView` stays valid after the `Client` is destroyed | probe: add a case to tests/detent_local/test_client_source.cc that copies `client->read_view()`, destroys the client, then reads; tests/detent/test_read_view.cc:11-19 covers only the in-memory snapshot client.
- `reference/cpp-api/sources.md:L89-93` | clients with a read source "never fall back on a type or conversion error" | probe: `ctest -R test_client_source -V` and read the wrong-type-in-live, present-in-defaults case (code path: src/detent_local/client_source.cc:43-57, depends on `ReadView::try_eval` returning empty only for absent keys).
- `reference/bindings/python.md`, `rust.md` | the binding tests pass on main | probe: `touch bindings/python/detent_python.cc bindings/rust/src/lib.rs`, then `cmake --build build --target detent_python_test detent_rust_test` with `-DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON`.
- `faq.md:L7-8` | "Within one poll interval of the source changing ... 200 ms by default" end to end | probe: quick-start authority plus managed agent, publish a knob and poll `detent get` at 10 ms, record publish-to-visible latency over 100 runs as a `quiet strict` job (code only proves the default, src/detent_delivery/configuration.cc:208)
- `faq.md:L14-15` | "Processes that start during the outage serve their baked defaults" | probe: stop detentd, restart `detent-agent --managed ... --generation-dir` on an existing store, run `detent get` with and without `--default`; expect the last durable generation, not baked defaults, for managed readers
- `help.md:L16,L21` and `contributing.md:L11,L18` | Issues, Discussions, issue form and security advisories are enabled | probe: `gh api repos/Kronuz/Detent --jq '{i:.has_issues,d:.has_discussions}'` and `gh api repos/Kronuz/Detent/private-vulnerability-reporting`
- `contributing.md:L24-27` | the three build commands pass on a clean checkout | probe: run them as a `quiet light` job (docs-e2e will own this)
- `next.md:L16-23` | teardown output and `durable revision=13 knob_count=3` sample | probe: run the quick-start to its last step and diff (docs-e2e)
- `roadmap.md:L19-22,L36,L81-83` | component-acceptance statements for Stages 3-5 | probe: each is a landed quest in `.campaign/detent/state.json` (verified there) but no committed `docs/status`, `docs/perf` or `benchmarks` file carries the test counts they imply
- `docs/AUTHORITY_SCHEDULING_ARCHITECTURE.md:L3` | "implementation and comparative qualification pending" | probe: `git log -S"entropy" -- src/detent_authority/node.cc src/detent_authority/service.cc` and compare owner-turn counters in benchmarks/
- `docs/CONSOLIDATION_RESPONSE.md:L3` | status "coordinated implementation pending", CL-1 to CL-11 | probe: `git grep -n "crc32c\|OwnerLock" src include` against the cpp-libraries consolidation-2026-10-19 review
- `docs/*.md` (all 30) | every benchmark, timing and test-count figure in these records | probe: not re-verified; each cites a file under benchmarks/, docs/measurements/ or docs/reviews/, check with `python3 -I -c` over their JSON when the owner wants the numbers re-attested
- `docs/USAGE.md:L58-59` | Scenario 1 prints `search:max_results = 25` and `checkout:killswitch_new_path = false` | probe: build `detent` examples and run the snippet with the shown `defaults.json` (docs-e2e runner)
- `docs/USAGE.md:L107-110` | rollout 10% gives `user-1` false and `user-42` true | probe: evaluate `stable_bucket("payments:new_processor","user-1"/"user-42") < 10` via `detent get payments:new_processor --hash user-N` against a store holding the L96-101 knob
- `docs/USAGE.md:L216-222` | `--initialize-managed` creates missing directories with child/parent barriers; later starts require an existing store | probe: run `detent-agent --managed m.json --generation-dir <new> --initialize-managed`, then without the flag on an empty dir
- `docs/SERVICES.md:L88` | "measured approximately 60-second 5M full recovery" | probe: a 5M full-recovery open under `quiet exclusive`; `docs/perf/cold-bootstrap-5m.md` measures cold bootstrap (754 s), not recovery, and no committed JSON carries it (`FAST_CLIENT_ARCHITECTURE.md` quotes 47.8-58.8 s)
- `docs/SERVICES.md:L42-48` | launchd `prepare-runtime` recreates only the runtime directory; rc.d start hook recreates service dirs | probe: read-only check of `packaging/launchd/org.detent.prepare-runtime.plist` and `packaging/rc.d/detent_agent` on macOS/FreeBSD CI
- `docs/RELAY.md:L126` | 500-client network qualification needs soft descriptor limit >= 4096 | probe: `grep -n 'RLIMIT_NOFILE\|4096' tests/detent_delivery/relay_network_fixture.hh` and run the relay network CTest under `ulimit -n 1024`
- `docs/RELAY.md:L120-122` | shared disk scheduling and queue-wait health fields | probe: `curl` relay `/v1/health` (via `detent-relay --status`) and compare keys with `src/detent_relay/host.cc`; the code lists no queue-wait key
- `docs/SCALE_TARGETS.md:L142` | "C++ in-process `detent::Client` 149 ns" and "`detent get` 5.2 +/- 0.4 ms (300 runs, hyperfine)" | probe: `quiet exclusive ... build/benchmarks/detent_read_bench`; `hyperfine -N 'detent get search:max_results'` on a warm agent; no committed file carries either
- `docs/HOW_IT_WORKS.md:L127,L165` | compaction every 60 s and scrub at 524,288 B/s behave as described | probe: run an agent against a relay for 2 minutes and read the coordinator status and `scrub_passes`
- `docs/LOGGING.md:L42` | warning/error bypass the 4,096 pending limit, critical logs go synchronously | probe: logger library source (`Kronuz/logger`) at the pinned revision in `cmake/dependencies.json` (`detent_logger`); Detent sets only `max_pending = 4096` (`src/detent_cli/logging.cc:64`)
- `docs/PRODUCTION_CONTRACT.md:L38` | "roughly 117 MB" per 500,000-key image | probe: `benchmarks/paged-delivery-measurements.json:45` records 117,895,168 mapped_peak, not image size; run `detent_local_image_bench` at 500,000 knobs


## Owned by other quests

Pages a not-yet-landed quest changes. The area quests re-check these rows after the owning quest lands, and do not rewrite the named paragraphs before it does unless they coordinate with it. `docs-e2e` is also listed: it adds assertion blocks to pages and owns `tests/docs/e2e_exceptions.txt`, which the area quests shrink.

| Quest | State | Rows | Race to avoid |
|---|---|---|---|
| `async-drivers` | planned (design in review: async-drivers-design) | 3 | Owner loops run on coroutine drivers: wording about workers and the Worker API. |
| `daemon-ready-leader` | active | 6 | `detentd` reports ready only once a leader exists: `--daemonize`, `--status`, quick-start waiting text. |
| `dense-pages` | active | 15 | One format (DTPAGE03/DTROOT03); the local-image, limits and delivery pages restate its bytes and bounds. |
| `docs-e2e` | active | 1 | Assertion blocks on pages and the exceptions files. |
| `legacy-h` | blocked | 51 | The largest: every page that describes the Client, the defaults file or `detent get --defaults`. Rewrite those pages once, after it lands; until then write them as true of `main` without new Client examples. |
| `legacy-i` | planned | 14 | Removes `flat-v1`, `image_format`, `legacy_anchor`, `paged-v2`; deletes the config key and every per-format sentence. |
| `namespace-s3b` | blocked | 6 | Repair of a damaged pack: delivery, errors (`SourceFailure`) and the trusted-restart text. |
| `namespace-s4-s10` | planned | 5 | Per-knob summaries upstream on relay sessions: relay, authority and delivery text. |
| `observability` | design landed (observability-design); OB* quests not created | 12 | `detent_usage`, `detent-agent --status --usage`, the Prometheus textfile, the usage page: observability, status and architecture text. |
| `rc1-integration to rc5-release-candidate` | planned | 2 | Roadmap and status wording. |
| `rc3-sustained-capacity` | planned | 1 | Operating envelope and limits. |
| `rc4-restoration` | planned | 1 | Restoration and recovery text. |
| `rc5-release-candidate` | planned | 5 | Security (D47 #25), upgrade text, release wording. |
| `reader-s6` | blocked | 14 | Adds `libdetent_reader`, the C ABI and `Reader`; pages that gain a Reader section wait for it. |
| `reader-s7-s9` | planned | 17 | `detent get` moves to the reader core: error text, exit statuses and `--status` restate. |

Rows by quest (page: what changes when it lands):

#### `async-drivers`

- `architecture/authority.md`: L96, L108-115: owner loop and bounded-turn wording move to coroutine drivers; the callback Worker API is gone
- `roadmap.md`: Build and Release list gains M1b; L5-8 status wording
- `docs/ASYNC_AUTHORITY_ARCHITECTURE.md, docs/AUTHORITY_ARCHITECTURE.md, docs/AUTHORITY_SCHEDULING_ARCHITECTURE.md`: Worker API and owner-loop text (L111-144, Worker references, Node::service scheduling)

#### `daemon-ready-leader`

- `architecture/authority.md`: `detentd --daemonize` and `--status` report ready only once a leader exists
- `quickstart.md`: the retry loop (L101), the `outcome_unknown` transcript (L109-115) and L104-107 go.
- `QUICKSTART.md`: the open-retry loop and transcript (L68-79); the Stop block's `--status` output when `--usage` lands.
- `guides/durable-authority.md`: `--daemonize`/`--status` readiness: returns and reports `"ready":true` only once a leader accepts `/v2/open`; the first-open retry (if documented) goes away.
- `reference/cli.md`: `--daemonize` readiness wording for `detentd`
- `reference/cli/detentd.md`: `--daemonize` returns and `--status` reports `"ready":true` only once a leader accepts `/v2/open`

#### `dense-pages`

- `architecture/delivery.md`: L53-65: DTPAGE03/DTROOT03, one image format, refuse and refetch
- `architecture/design-documents.md`: L32 LOCAL_IMAGE_FORMAT.md description; the doc itself is rewritten
- `architecture/how-it-works.md`: L208-247 page layout, two digests, Merkle, Overflow kind; L117-136 file sizes if the root or records change
- `basics.md`: Snapshot, Version, Baked Defaults, Source, Override sections describe the Client; `detent get` has no `--defaults` until S7 (D21); version vs cursor revision definition moves with dense pages.
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `guides/managed-delivery.md`: `image_format` row, "Paged local publication" section, "flat-v1"/"paged-v2" names, refuse-and-refetch of any other store; `--upgrade-managed` wording.
- `operations/upgrading.md`: the page is deleted; its format table (local image layout, `flat-v1`) and C++ ABI notes do not survive.
- `reference/configuration.md`: one image format: any per-format or per-version limit and wording
- `reference/limits.md`: the "Local Image Format" rows and `pages`/`segments` bounds are restated for Dense v3 (DTPAGE03/DTROOT03)
- `reference/snapshot-format.md`: Only if a local-image section is added: DTPAGE03/DTROOT03 become the single image format, with refuse-and-refetch for any other; the DTDIST01 wire format is unaffected. Also legacy-h: L5-6 "Defaults files" and L58 `source_poll`/`is_patch` references disappear with `--defaults` and the Client sources.
- `roadmap.md`: L83 format configuration/migration sentence, L106 paged-delivery sentence
- `docs/ACTORS.md`: L105-117 generation table and `image_format` paragraph
- `docs/CAPACITY_QUALIFICATION.md, docs/CAPACITY_RUNTIME.md, docs/DISTRIBUTION_ARCHITECTURE.md`: `image_format`, `paged-v2`, `flat-v1`, pages/segments limits (legacy-i A1 greps docs)
- `docs/LOCAL_IMAGE_FORMAT.md`: whole format section: flat-v1, `image_format`, DTPAGE02/DTROOT02 replaced by DTPAGE03/DTROOT03; refuse-and-refetch
- `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md, docs/PAGED_DELIVERY_REVIEW.md, docs/PREPARED_ACTIVATION_ARCHITECTURE.md, docs/PRODUCTION_CONTRACT.md, docs/IMPLEMENTATION.md`: `flat-v1`, `image_format`, `legacy_anchor` (IMPLEMENTATION L371), v1/v2 coexistence

#### `docs-e2e`

- `contributing.md`: "Document as you go" (L44-45) and the website section name the docs check and E2E gate; any edit voids tested_docs evidence

#### `legacy-h`

- `architecture.md`: L24, L49, L73-80: `detent::Client`, file source and `selected` poll become `Reader` over `libdetent_reader`; no per-client background thread
- `architecture/agent.md`: L12: "the C++ managed source" and `detent get` through `detent::Client`; `detent get` moves to the reader core (no `--defaults`)
- `architecture/dependencies.md`: L13-31 diagram and targets table (`detent` core client, `detent::local_client`) change with the Client and reader library
- `architecture/how-it-works.md`: L49 diagram, L78-84, L274-316: `libdetent_reader` lands; "Today on main" paragraph and the Client references go
- `architecture/read-path.md`: whole page: `Client`, `SnapshotRef`, `OverrideTable`, `ClientReadView`, `InitOptions`; the read API is `Reader`
- `architecture/update-path.md`: whole page: the Client updater thread, file/poll sources, `on_update`, `InitOptions`
- `about.md`: L26 snapshot-file updates, L36 library and bindings
- `benchmarks.md`: `detent_read_bench`, `detent_update_bench` drive `detent::Client` and are deleted or ported
- `index.mdx`: the C++ client card (L62) and bindings wording change to `Reader`; thin Python/Rust layers.
- `welcome.md`: L14 and L23 (`detent::Client`, background updater) rewrite around `Reader`/`libdetent_reader`.
- `basics.md`: Snapshot, Version, Baked Defaults, Source, Override sections describe the Client; `detent get` has no `--defaults` until S7 (D21); version vs cursor revision definition moves with dense pages.
- `installation.md`: Homebrew tap bump (D22, rc5 A8) and the library-only `detent::Client` build paragraph (L90-95); `detent_demo` goes with the Client.
- `settings-and-flags.md`: C++ `get`/`get_as`/`eval` snippets (L30-38, L62-70) and the baked-default fallback sentence (L93-95).
- `rollouts.md`: the C++ snippet (L31-33).
- `group-overrides.md`: the C++ snippet (L40-42) and evaluation step 1 (CLI override, L50).
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `guides.md`: "Reading Knobs" item 1 (client) and item 2 (Live Updates) change; Live Updates entry is deleted (legacy-h A14), a reader page replaces it.
- `guides/cpp-application.md`: every section: `Client`, `InitOptions`, `detent::global`, `read_view()`, `health()`, `defaults_path`, `source_path` give way to `Reader` over `libdetent_reader`; the FetchContent example changes with the targets.
- `guides/kill-switches.md`: `client.eval_or_panic` samples become `Reader` calls; the "Stale-flag detection" hint is replaced by the `detent_usage` / usage-page report.
- `guides/live-updates.md`: the whole page (Client sources, `source_path`, `source_poll`, `health()`) is deleted and replaced by a reader reference page.
- `guides/managed-delivery.md`: section 5 (`InitOptions`, `make_local_read_source`, `Client`, `current_snapshot()` throws) becomes `Reader`.
- `guides/python-and-rust.md`: both bindings are rebuilt on `libdetent_reader` (Python C extension, Rust crate over a C ABI); samples, the "wrap `detent::Client`" claim and the gap list change.
- `guides/shell-and-agent.md`: "Local Defaults" / `--defaults` / `defaults.json`: `detent get` drops `--defaults` until reader S7; S7-S9 reshape defaults handling.
- `guides/testing.md`: `TestKnobs`, `detent::global`, `Client(Snapshot)`, `InitOptions::cli_overrides`, defaults-only clients are deleted; testing moves to the reader's testing library (`detent_testing_*`).
- `operations/security.md`: item 4 (file source) is removed with the C++ file source.
- `operations/upgrading.md`: the page is deleted; its format table (local image layout, `flat-v1`) and C++ ABI notes do not survive.
- `reference/configuration.md`: `defaults_path`, `defaults.json` discovery, `DETENT_DEFAULTS_PATH` and the defaults sentence (L63, L73, L80-81, L141) go with the C++ defaults file
- `reference/limits.md`: the Client section (`InitOptions::max_knobs`, 16 MiB snapshot) is deleted or rewritten for the Reader
- `reference/cli/detent.md`: `--defaults`, `DETENT_DEFAULTS_PATH`, "answers from `--defaults`" (L28, L46, L74-75) are removed from `detent get` until reader-s7-s9
- `reference/cli/detent-agent.md`: no direct text; `--upgrade-managed` remains an unowned D43 question
- `reference/knobs.md`: the "Read with" column and L117-118 baked-defaults merge rule move to the Reader and its caller-supplied defaults
- `reference/errors.md`: Read Errors, Failure Behavior and Update Rejections describe `Client`, `detent::global`, the constructor and baked defaults; rewritten for the Reader's status codes
- `reference/observability.md`: Client Health, Client Metrics, Suggested Alerts (`client.health()`, `client.metrics()`) are deleted with `Client`
- `reference/cpp-api.md`: Whole page: `detent::Client`, `InitOptions`, `HealthSnapshot`, `Metrics`, `global.hh`, `testing.hh`, `overrides.hh`, `read_view.hh`, `detent_local/client_source.hh` are deleted; replaced by `Reader` over `libdetent_reader` (legacy-h A14: `reference/cpp-api/reader.md`). Lead decides whether this page becomes the Reader page.
- `reference/cpp-api/client.md`: Deleted with the Client; legacy-h A14 removes the page.
- `reference/cpp-api/global-and-testing.md`: Deleted with `detent::global`, `TestKnobs`, `OverrideTable`; legacy-h A14 removes the page.
- `reference/cpp-api/sources.md`: Deleted with `source_path`/`source_poll`, `LocalReadSource` and both factories; legacy-h A14 removes the page.
- `reference/bindings.md`: "wrap the C++ `detent::Client`", the technology column (pybind11/cxx), and the hint about no reader ABI change when the C ABI, Python C extension and Rust crate land.
- `reference/bindings/python.md`: `detent.Client(defaults_path, ...)`, `DetentError`, `health()` replaced by `Reader`, `Status`, `ReaderError`, `Lookup`; `DETENT_BUILD_PYTHON` build path changes.
- `reference/bindings/rust.md`: `Client::new`, `DetentError(String)`, `HealthInfo` and the cxx build replaced by a crate over the C ABI.
- `reference/evaluation.md`: L5-6, L13, L56, L63 (`get_as`), L117 (`read_view()`) name Client APIs; the CLI-override step has no counterpart in the reader's `detent::local::evaluate` until `detent get` loses `--defaults`/overrides (D21).
- `faq.md`: L7-8 (200 ms Client default), L12-15 (baked defaults, `staleness_seconds`), L37-39 ("Do I need the agent?") are written for `detent::Client` and rewritten around `Reader`
- `next.md`: L31-32 "ship baked defaults" and "link the library" bullet
- `roadmap.md`: L14-17 Client row Done, L68-77 Client and Bindings list (`on_update`, defaults, flag parser)
- `docs/ACTORS.md`: L22, L36, L129 `detent::Client`, `InitOptions`, `read_source`
- `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md, docs/DISTRIBUTION_ARCHITECTURE.md`: `ClientReadView`, `read_source`, `Client::on_update`
- `docs/USAGE.md`: Scenario 1 and "Scoped reads" (`Client`, `InitOptions`, `read_view`, `make_local_read_source`), `--defaults`/`defaults.json`/`DETENT_DEFAULTS_PATH`, `detent get` implementation: rewrite to `Reader`
- `docs/HOW_IT_WORKS.md`: L17, L34, L103, L115 (Client today, `libdetent_reader` "built on branch")
- `docs/SERVICES.md`: L25 `defaults.json`, L35 `detent get`, L52 `--default`
- `docs/RELAY.md`: L80 "local defaults"
- `docs/FAST_CLIENT_ARCHITECTURE.md, docs/FLEET_QUALIFICATION.md, docs/HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md, docs/SCALE_TARGETS.md, docs/SCALING.md, docs/DOCUMENTATION_FINDINGS.md`: `detent::Client` / `InitOptions` / `detent::local_client` / poll source mentions

#### `legacy-i`

- `architecture/delivery.md`: L53-65, L94-97: one image format; `paged-v2`, `flat-v1`, `image_format` removed
- `architecture/design-documents.md`: L32 LOCAL_IMAGE_FORMAT.md description; the doc itself is rewritten
- `benchmarks.md`: L49 "Flat image encode"
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `guides/managed-delivery.md`: `image_format` row, "Paged local publication" section, "flat-v1"/"paged-v2" names, refuse-and-refetch of any other store; `--upgrade-managed` wording.
- `operations/upgrading.md`: the page is deleted; its format table (local image layout, `flat-v1`) and C++ ABI notes do not survive.
- `reference/configuration.md`: L99 (`image_format`, `flat-v1`, `paged-v2`, "Recovery reads either format") is deleted; an `image_format` key is refused as unknown
- `reference/limits.md`: any limit named for `flat-v1`/`paged-v2`
- `roadmap.md`: L83 format configuration/migration sentence, L106 paged-delivery sentence
- `docs/ACTORS.md`: L105-117 generation table and `image_format` paragraph
- `docs/CAPACITY_QUALIFICATION.md, docs/CAPACITY_RUNTIME.md, docs/DISTRIBUTION_ARCHITECTURE.md`: `image_format`, `paged-v2`, `flat-v1`, pages/segments limits (legacy-i A1 greps docs)
- `docs/USAGE.md`: any `image_format` text; `reader_validation: "full"` "flat" wording (L250)
- `docs/LOCAL_IMAGE_FORMAT.md`: whole format section: flat-v1, `image_format`, DTPAGE02/DTROOT02 replaced by DTPAGE03/DTROOT03; refuse-and-refetch
- `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md, docs/PAGED_DELIVERY_REVIEW.md, docs/PREPARED_ACTIVATION_ARCHITECTURE.md, docs/PRODUCTION_CONTRACT.md, docs/IMPLEMENTATION.md`: `flat-v1`, `image_format`, `legacy_anchor` (IMPLEMENTATION L371), v1/v2 coexistence

#### `namespace-s3b`

- `architecture/how-it-works.md`: L495-499 repair: damaged packs heal with no operator action
- `guides/managed-delivery.md`: damaged-pack repair: `managed scrub found N damaged packs` and the `scrub_bytes_per_second` description gain a repair path.
- `reference/errors.md`: damaged-pack reads map to `SourceFailure`; the category is added to the table
- `roadmap.md`: L81-83 Distribution list (repair of damaged packs, per-namespace subscriptions)
- `docs/ACTORS.md`: scrub and damaged-pack repair text joins the `.live` addition
- `docs/HOW_IT_WORKS.md`: L16, L18-20, L61, L129-137, L145-153, L167 (repair, `selected` table, compacted blocks, `detent-image`)

#### `namespace-s4-s10`

- `architecture/how-it-works.md`: L25-27, L146-152, L376-447: namespace table, per-namespace `change` slots, compacted blocks, seeding, `detent-image`
- `architecture/relay.md`: per-namespace sessions, upstream per-knob usage summaries (design Q4)
- `guides/durable-relay.md`: relay mirror of blocks/base/ring (S8) and per-knob summaries upstream on relay sessions (D47 Q4) change what the relay serves and reports.
- `roadmap.md`: L81-83 Distribution list (repair of damaged packs, per-namespace subscriptions)
- `docs/HOW_IT_WORKS.md`: L16, L18-20, L61, L129-137, L145-153, L167 (repair, `selected` table, compacted blocks, `detent-image`)

#### `observability`

- `architecture.md`: gains `detent_usage`, `detent-agent --status --usage`, the Prometheus textfile
- `architecture/agent.md`: `--status --usage` and the usage page
- `architecture/relay.md`: per-namespace sessions, upstream per-knob usage summaries (design Q4)
- `benchmarks.md`: usage-recording overhead rows (OB1)
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `QUICKSTART.md`: the open-retry loop and transcript (L68-79); the Stop block's `--status` output when `--usage` lands.
- `guides/durable-relay.md`: relay mirror of blocks/base/ring (S8) and per-knob summaries upstream on relay sessions (D47 Q4) change what the relay serves and reports.
- `guides/kill-switches.md`: `client.eval_or_panic` samples become `Reader` calls; the "Stale-flag detection" hint is replaced by the `detent_usage` / usage-page report.
- `guides/shell-and-agent.md`: reader construction/validation text; `detent-agent --status --usage` and the Prometheus textfile.
- `reference/observability.md`: `detent_usage`, `detent-agent --status --usage`, the Prometheus text file and the usage page (docs/design/observability.md Q1-Q7, D47); the "no exporter" hint changes when they land (OB* quests not yet created)
- `roadmap.md`: L87-88 metrics endpoint item becomes the textfile plus `--status --usage` (D47 Q3)
- `docs/LOGGING.md`: `detent_usage`, Prometheus textfile and `--status --usage` will add log/metric surface (design `docs/design/observability.md`)

#### `rc1-integration to rc5-release-candidate`

- `roadmap.md`: L19-23 Status at a Glance and L45-55 Build and Release take the RC-1 to RC-5 exits
- `docs/RELEASE_PLAN.md`: RC-1..RC-5 exits and component table replaced by the campaign quests

#### `rc3-sustained-capacity`

- `reference/limits.md`: operating envelope and measured limits (docs/CAPACITY_QUALIFICATION.md) replace or extend the constants

#### `rc4-restoration`

- `docs/RESTORE_ARCHITECTURE.md`: fresh three-voter restore host (admin tool no longer exists)

#### `rc5-release-candidate`

- `installation.md`: Homebrew tap bump (D22, rc5 A8) and the library-only `detent::Client` build paragraph (L90-95); `detent_demo` goes with the Client.
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `operations/authority.md`: voter `host` accepts DNS names (D47 #26); the voter set stays static and pinned.
- `operations/security.md`: states no at-rest encryption and recommends disk or volume encryption (D47 #25); voter DNS names.
- `reference/configuration.md`: D47 #26 DNS voter addresses become documentable only when the voter loader resolves names

#### `reader-s6`

- `architecture.md`: L24, L49, L73-80: `detent::Client`, file source and `selected` poll become `Reader` over `libdetent_reader`; no per-client background thread
- `architecture/how-it-works.md`: L49 diagram, L78-84, L274-316: `libdetent_reader` lands; "Today on main" paragraph and the Client references go
- `about.md`: L26 snapshot-file updates, L36 library and bindings
- `welcome.md`: L14 and L23 (`detent::Client`, background updater) rewrite around `Reader`/`libdetent_reader`.
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `guides.md`: "Reading Knobs" item 1 (client) and item 2 (Live Updates) change; Live Updates entry is deleted (legacy-h A14), a reader page replaces it.
- `guides/cpp-application.md`: every section: `Client`, `InitOptions`, `detent::global`, `read_view()`, `health()`, `defaults_path`, `source_path` give way to `Reader` over `libdetent_reader`; the FetchContent example changes with the targets.
- `guides/managed-delivery.md`: section 5 (`InitOptions`, `make_local_read_source`, `Client`, `current_snapshot()` throws) becomes `Reader`.
- `guides/shell-and-agent.md`: reader construction/validation text; `detent-agent --status --usage` and the Prometheus textfile.
- `next.md`: L16-20 `detent-agent --status` output if the read path moves to the reader core
- `roadmap.md`: L17 `detent get` row, L75 "stable reader ABI", L72-74 binding gaps
- `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md, docs/DISTRIBUTION_ARCHITECTURE.md`: `ClientReadView`, `read_source`, `Client::on_update`
- `docs/USAGE.md`: `detent get` through `GenerationReader`, no `--defaults` until S7; `Reader` as the read API
- `docs/HOW_IT_WORKS.md`: L17, L34, L103, L115 (Client today, `libdetent_reader` "built on branch")

#### `reader-s7-s9`

- `architecture/agent.md`: L12: "the C++ managed source" and `detent get` through `detent::Client`; `detent get` moves to the reader core (no `--defaults`)
- `architecture/how-it-works.md`: L49 diagram, L78-84, L274-316: `libdetent_reader` lands; "Today on main" paragraph and the Client references go
- `about.md`: L26 snapshot-file updates, L36 library and bindings
- `index.mdx`: the C++ client card (L62) and bindings wording change to `Reader`; thin Python/Rust layers.
- `basics.md`: Snapshot, Version, Baked Defaults, Source, Override sections describe the Client; `detent get` has no `--defaults` until S7 (D21); version vs cursor revision definition moves with dense pages.
- `settings-and-flags.md`: C++ `get`/`get_as`/`eval` snippets (L30-38, L62-70) and the baked-default fallback sentence (L93-95).
- `README.md`: sections 3, 4, 6 (Client, InitOptions, global, TestKnobs); L212 image-format paragraph; L304-335 bindings; L229-238 binaries table (`detent_demo`, tests); L346-354 benchmarks (`detent_read_bench` retired); L364-372 readiness table at RC; agent `--status --usage` (observability).
- `guides/shell-and-agent.md`: "Local Defaults" / `--defaults` / `defaults.json`: `detent get` drops `--defaults` until reader S7; S7-S9 reshape defaults handling.
- `reference/cli/detent.md`: `detent get` reads through the reader core; error text, exit statuses and `SourceFailure` handling restate
- `reference/cli/detent-agent.md`: `--status` and reader flags read through the reader core
- `reference/bindings.md`: "wrap the C++ `detent::Client`", the technology column (pybind11/cxx), and the hint about no reader ABI change when the C ABI, Python C extension and Rust crate land.
- `reference/bindings/python.md`: `detent.Client(defaults_path, ...)`, `DetentError`, `health()` replaced by `Reader`, `Status`, `ReaderError`, `Lookup`; `DETENT_BUILD_PYTHON` build path changes.
- `reference/bindings/rust.md`: `Client::new`, `DetentError(String)`, `HealthInfo` and the cxx build replaced by a crate over the C ABI.
- `faq.md`: L37-39 agent vs `detent get` answer follows the reader-core `detent get`
- `roadmap.md`: L17 `detent get` row, L75 "stable reader ABI", L72-74 binding gaps
- `docs/USAGE.md`: `detent get` through `GenerationReader`, no `--defaults` until S7; `Reader` as the read API
- `docs/SERVICES.md`: L25 `defaults.json`, L35 `detent get`, L52 `--default`


## Inputs

Owner inputs the area quests place. Each is placed in a named area's scope.

1. **Glossary page (Xapiand triage #24).** A glossary page in area 5 (`reference/glossary.md`, in the sidebar's Reference Guide) defines: authority, voter, leader, relay, agent, namespace, generation, store, page, knob and rollout. Write from: `docs/ACTORS.md` "Terms" (authority, voter, relay, agent, generation, producer), `docs/design/namespace-delivery.md` and `include/detent/model.hh` (namespace, knob), `docs/design/dense-pages.md` and `docs/LOCAL_IMAGE_FORMAT.md` (store, page), `website/src/content/docs/rollouts.md` and `docs/mechanisms/evaluation.md` (rollout), the consensus module in `src/detent_authority/node.hh` (leader). No page or glossary exists on `main` (`grep -rli glossary website README.md docs` finds nothing). Define each term once, link each use on a page to it, and keep the store and page entries to Dense v3 once `dense-pages` lands.
2. **Disk or volume encryption on the security page (D47 #25).** `operations/security.md` is in area 3. It states that Detent has no at-rest encryption, that knob values are not secrets, and that disk or volume encryption is the remedy; voter replication is already mutual TLS with pinned certificates (the page says so at L22-24). The page lacks the first sentence today (audit row for `operations/security.md`).
3. **DNS voter addresses in the configuration reference (D47 #26).** `reference/configuration.md` is in area 4. D47 says voter addresses may be DNS names, but `voters[].host` accepts IP literals only: `src/detent_authority/service.cc:381` parses it with `asio::ip::make_address`, and a name throws. The same holds for `publisher_bind`, the managed `address`, `authority_endpoints` and the relay `address`. The reference must say "IP address" until a quest changes the loader; it must not document DNS names now. No open quest implements the loader change (`rc5-release-candidate` lists it only as an input), so the lead decides whether the loader change is a new quest before the configuration quest runs (see "## Code questions the audit raised").
4. **No compatibility text (D43).** No rewritten page carries "legacy", "previously", "since vN", "old format" or "migrating" text. The audit lists the existing instances per page.

## Site infrastructure (not a page)

- Sidebar: `website/src/lib/sidebar.mjs` lists all 61 content pages by slug; `index.mdx` is the splash. No orphan page.
- One broken internal link: `guides/durable-relay.md:31` links `managed-delivery.md` (a relative file name with the extension; Starlight serves `/Detent/guides/managed-delivery/`). Every other internal link in the 62 pages resolves to a page (checked by a one-off script over `](...)` links; heading anchors were not checked, the `site` check settles them).
- `website/astro.config.mjs:72` says `rest` blocks copy as curl "against a local distributor on its default port"; the distributor is removed (the host is `http://localhost:8317`, the authority's default port, `src/detent_cli/config.cc` `kDefaultPort`).
- `website/astro.config.mjs:49` `editLink.baseUrl` is `https://github.com/Kronuz/Detent/edit/main/website/`; the pages are under `website/src/content/docs/`, so the link resolves through the custom `src/components/EditLink.astro`; not verified in a browser.
- `.github/workflows/pages.yml` deploys on pushes to `website/**`; `ci.yml` ignores `docs/**`, `website/**` and `*.md` (`.github/workflows/ci.yml:5`); no workflow checks the pages' commands or links yet (`docs-e2e` adds `.github/workflows/docs.yml`).
- `website/README.md` is accurate for the toolchain (Astro, Starlight, D2 CLI, `rest` fences) and carries no stale text.

## Code questions the audit raised

1. D47 #26 versus `service.cc:381`: DNS names for voters need a loader change that no quest owns. Options: a quest in the rc5 wave that resolves names at start (and its page text), or write "IP address" now. Recommendation: write "IP address" now; the owner decides the loader.
2. `--upgrade-managed` (`src/detent_agent/main.cc:116`, `StoreOpenMode::UpgradeLegacy`) is legacy adoption, against D43; `legacy-i` removes `flat-v1`, `image_format`, `legacy_anchor` and `paged-v2` but not this flag. Options: add it to `legacy-i`'s scope, or keep it as a documented one-case mode. Recommendation: remove it with `legacy-i`.
3. `welcome.md` is in none of the area quest files; the audit places it in area 2 (features and quick start). Confirm.
4. `detent-relay` has no CLI reference page and `cli.md` says "three programs" while four binaries exist (`src/CMakeLists.txt`, relay behind `DETENT_BUILD_RELAY`, default OFF). Area 5 adds `reference/cli/detent-relay.md`; confirm the build option's default is intended.
5. The premise that `detent::Client` and the defaults file are removed is not yet true of `main` (see Summary 1); the area quests that start before `legacy-h` lands rewrite Client pages twice. The lead orders them.
