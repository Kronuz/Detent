# Documentation findings and feature gaps

**Date:** 2026-10-09
**Context:** Written while building the public documentation website in
`website/` (published at https://kronuz.github.io/Detent/). Every item below
was found by reading the source, running the built binaries, or attempting a
clean build. The website already documents each user-visible gap with an
"unimplemented" note and collects them on its Roadmap page
(`website/src/content/docs/roadmap.md`). When an item is resolved, please update
that page and remove the matching note.

Items are ordered by priority within each section.

## 1. Blockers

### 1.1 A clean clone cannot build from public sources

Reproduced on 2026-10-09 by cloning this repository into a scratch directory
and running `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
with no `FETCHCONTENT_SOURCE_DIR_*` overrides:

1. Configure fails: `Target "detent_delivery_coordinator" links to:
   detent::local_publication but the target was not found`
   (`src/CMakeLists.txt:46`).
2. Build fails: `include/detent_local/storage_session.hh:5:10: fatal error:
   'completion/api.h' file not found`.

The local `build/` works only because `CMakeCache.txt` points
`FETCHCONTENT_SOURCE_DIR_{CLUSTER,HTTP_LIB,REACTOR,SHA256,CPP_BTREE}` at sibling
checkouts. Those siblings are ahead of their upstreams by: `cluster` 69
commits, `http` 8, `reactor` 5, `sha256` 2, `cpp-btree` 1 (all clean working
trees). `RAPIDJSON` also points at `.scratch/rapidjson-modern`.

**Needed:** publish those revisions (or vendor/pin them), then pin every
`FetchContent_Declare` to a tested commit instead of `main`/`master`. The
website's Installation page carries a warning and the override recipe until
this is fixed.

### 1.2 No license file

The repository is now public on GitHub with no `LICENSE`. The website's
Copyright page says no license is granted yet. Choose and add one (Xapiand and
the Kronuz libraries use MIT).

## 2. Discrepancies in existing repository docs

1. `README.md` still says "This repository has no public remote configured yet"
   and uses `GIT_REPOSITORY <this-repo-url>`. The URL is now
   `https://github.com/Kronuz/Detent.git`.
2. `README.md` test counts disagree with each other and with the tree: it says
   "15 C++ test binaries", lists `tests/detentd/` as both 4 and 5 suites, and the
   Layout block lists only `detent/`, `detentd/`, `detent_agent/`. The tree also
   has `detent_http/`, `detent_local/`, `detent_delivery/`,
   `detent_distribution/` and `detent_authority/`, and
   `docs/IMPLEMENTATION.md` reports 69 tests.
3. `README.md` attributes the 5.4x shared-memory read speedup to avoiding JSON
   parsing and a daemon round trip, but `detent_read_bench` doesn't parse JSON
   or make round trips per read either. The cause isn't established; the
   website states only the measured ratio.
4. `diagrams/03_gossip.d2` describes version-number gossip followed by a pull
   `GET`, which predates the Raft write path in `src/detentd/CLUSTERING.md`.
5. `diagrams/08_detent_deps.d2` describes `detent_agent_lib` as "Unix-socket
   agent server logic" (it's shared memory now), routes as `GET/PUT /knobs`
   (they're `/v1/snapshot`), `random` as a "rollout-hash bucketing PRNG" (it
   only drives backoff jitter; the bucket is SHA-256), and lists an `epoch`
   dependency that `CMakeLists.txt` never fetches.
6. `README.md`/`USAGE.md` cluster examples still use `/tmp/jk-*.json` file
   names from the JustKnobs era.
7. `CMakeLists.txt` `project()` has no `VERSION`, and there are no tags, so
   nothing reports a Detent version. The website header shows the commit hash
   instead.

## 3. Behavior worth fixing

1. **Confusing error for an unknown `detent get` flag.** `detent get NAME
   --bogus` prints `unknown argument` and then `detent-agent: cannot
   auto-start: detent-agent startup failed`, which suggests an agent problem.
   Report only the argument error.
2. **Legacy agent capacity vs. client limits.** The client accepts up to
   100,000 knobs and 16 MiB, but the legacy shared-memory mapping is a fixed
   4 MiB, so large snapshots are accepted by the agent's client and then fail to
   publish (logged as a capacity failure). Either size the mapping from the
   accepted snapshot or reject earlier with a clear message.
3. **`detent_publish` output noise.** Plain bool knobs are written as
   `"overrides":{},"rollout_pct":null`. Valid, but omit empty fields to match
   what `detentd` serves.
4. **`detent get` silently ignores `--hash`/`--switch` for int knobs.** Matches
   the spec, but a one-line stderr note would catch mistakes.

## 4. Specification items not implemented

From `justknobs-implementation-spec.md`; each is marked unimplemented on the
website.

1. Python `check()` alias and category-specific exceptions (`KeyError`,
   `ValueError`, `SystemError`, `OverflowError`); the binding raises one
   `DetentError`.
2. `allow_test_disable_env` and the deterministic test-disable environment
   switch (Section 11).
3. A shipped `--just-knob name=value` flag parser and `--just-knobs-config-path`
   (Section 11); only `parse_cli_overrides()` exists.
4. File source watching the parent directory with 100 ms coalescing
   (Section 10); the implementation polls mtime/size.
5. Metric export with the Section 12 names (`jk_reads_total`,
   `jk_snapshot_staleness_seconds`, ...); counters exist but nothing exports
   them.

## 5. Binding gaps

1. Python and Rust expose only the file source: no HTTP poll source, no
   managed generation source, no `read_view()`.
2. No `get_as`, no `_or_panic`, no Rust error enum, no Rust test guard.
3. No `.pyi` stubs, no wheel, no crate publication.
4. No Go or Java bindings (the spec mentions both), and no documented stable
   reader ABI for the shared snapshot.

## 6. Features worth considering

Proven useful in other configuration systems (Xapiand's own tooling,
LaunchDarkly, Unleash, Configerator-style systems, etcd/Consul-backed setups),
and noted on the website's Roadmap:

1. **Publishing CLI** (`detent set NAME VALUE`, `detent publish FILE`,
   `detent rollout NAME PCT`) so operators don't hand-write `curl` with version
   bookkeeping. The README's quick start is entirely `curl`.
2. **Audit history and rollback**: who changed what and when, list versions,
   revert to version N. The authority has revisions and receipts but no query
   surface for history.
3. **Dry-run validation** endpoint/flag that validates a snapshot or
   transaction without publishing.
4. **Metrics endpoint** (`/metrics`, OpenMetrics) on both daemons, plus a
   small exporter helper for client `Metrics`.
5. **Knob metadata**: owner, description, created/expires, and a stale-flag
   report. The kill-switch lifecycle relies on removal, but nothing helps find
   candidates.
6. **More value types**: string, float, JSON; and group overrides for int knobs.
7. **Targeting rules** beyond exact group names (attribute predicates,
   segments), and **scheduled changes** (apply at a time, or ramp on a schedule).
8. **Change notification API** for applications. `on_update` exists but is
   documented as the agent's hook, is exclusive with `read_source`, and isn't
   called for the initial snapshot.
9. **Optional bearer-token check on the classic distributor**, a cheap
   improvement for deployments that can't adopt the authority yet.
10. **Packaging**: Homebrew tap and container image (Xapiand ships both and its
    docs lead with them), Debian/RPM, FreeBSD port.
11. **CI** on Linux/macOS/FreeBSD with sanitizers, once 1.1 is fixed. The
    website's header shows the Pages build badge only, because there is no CI
    workflow to point at.
12. **Versioned schema for the authority health document**, so dashboards can
    depend on it.

## 7. Keeping the website in sync

The website lives in `website/` (Astro + Starlight, the same theme as the
Xapiand docs) and deploys from `main` through
`.github/workflows/pages.yml` whenever `website/**` changes. When changing
user-visible behavior, please update the matching page:

| Change | Page |
| --- | --- |
| CLI flags or output | `website/src/content/docs/reference/cli/*.md` |
| HTTP endpoints, headers, status codes | `website/src/content/docs/reference/http-api/*.md` |
| Config fields, env vars, directories | `website/src/content/docs/reference/configuration.md` |
| Public C++ API | `website/src/content/docs/reference/cpp-api/*.md` |
| Limits | `website/src/content/docs/reference/limits.md` |
| Status of any item above | `website/src/content/docs/roadmap.md` |
