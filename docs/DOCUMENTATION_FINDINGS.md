# Documentation findings and feature gaps

**Date:** 2026-10-09
**Context:** Written while building the public documentation website in `website/` (published at https://kronuz.github.io/Detent/). Every item below was found by reading the source, running the built binaries, or attempting a clean build. The website already documents each user-visible gap with an "unimplemented" note and collects them on its Roadmap page (`website/src/content/docs/roadmap.md`). When an item is resolved, please update that page and remove the matching note.

Items are ordered by priority within each section.

## 1. Blockers

### 1.1 A clean clone cannot build from public sources (resolved 2026-10-09)

A fresh clone failed to configure (`detent::local_publication` missing) and to compile (`completion/api.h` not found), because the build depended on unpushed sibling work. Resolved by publishing it:

1. `http`, `reactor`, `sha256` and `cpp-btree` fast-forwarded to their local heads.
2. `cluster` had diverged: GitHub held three Sep 30 commits (the `is_alive()` contract docs, `LivenessTracker`, and a `length.h` shim over `Kronuz/varint`) that the local branch never pulled. Public `varint` still had the overflow bug that local `5f583e5` fixed, so the fix was first ported to `varint` (`c16d214`, with regression tests that abort on the old decoder), then GitHub main was merged in (`55b3ac3`, all 15 cluster tests pass). No history was rewritten, so cited cluster SHAs stay valid.
3. A fresh `git clone https://github.com/Kronuz/Detent.git` then configures, builds and passes all 74 tests with no `FETCHCONTENT_SOURCE_DIR_*` overrides.

**Local checkout note:** `~/code/Kronuz/cluster` is now one merge commit behind `origin/main` (a fast-forward `git pull` picks it up); its `length.h` becomes the `varint` shim and `CMakeLists.txt` gains the `varint` dependency.

**Still open:** pin every `FetchContent_Declare` to a tested commit instead of `main`/`master`.

### 1.2 No license file (resolved 2026-10-09)

The repository now includes `LICENSE` with the MIT License, chosen by German. The website's Copyright page links to it, and the missing-license roadmap entry is removed.

## 2. Discrepancies in existing repository docs

Items 2.1–2.6 are resolved (2026-10-10). The README uses the public repository URL, lists the current source and include modules, describes tests by enabled profile and removes unsupported attribution for the shared-memory benchmark ratio. The propagation diagram shows classic Raft replication; the dependency diagram identifies its classic-profile scope and corrects routes, shared-memory storage, retry randomness and the absent epoch dependency. Classic cluster examples use project-local `.scratch/` paths and create that directory before starting.

The project still has no semantic `VERSION` or release tags. The website displays the commit hash. Assigning a release version remains a release-policy decision; documentation does not invent one.

## 3. Behavior worth fixing

1. **Confusing error for an unknown `detent get` flag (resolved 2026-10-09).** The reader validates options before reading configuration or starting an agent. Unknown options, missing values and extra positional arguments produce one argument error and exit 1, including when `--default` is supplied. Negative integer defaults remain accepted. The CLI regression checks that invalid invocations create no runtime artifacts.
2. **Legacy agent capacity vs. client limits.** The client accepts up to 100,000 knobs and 16 MiB, but the legacy shared-memory mapping is a fixed 4 MiB, so large snapshots are accepted by the agent's client and then fail to publish (logged as a capacity failure). Either size the mapping from the accepted snapshot or reject earlier with a clear message.
3. **`detent_publish` output noise.** Plain bool knobs are written as `"overrides":{},"rollout_pct":null`. Valid, but omit empty fields to match what `detentd` serves.
4. **`detent get` silently ignores `--hash`/`--switch` for int knobs.** Matches the spec, but a one-line stderr note would catch mistakes.

## 4. Specification items not implemented

From `justknobs-implementation-spec.md`; each is marked unimplemented on the website.

1. Python `check()` alias and category-specific exceptions (`KeyError`, `ValueError`, `SystemError`, `OverflowError`); the binding raises one `DetentError`.
2. `allow_test_disable_env` and the deterministic test-disable environment switch (Section 11).
3. A shipped `--just-knob name=value` flag parser and `--just-knobs-config-path` (Section 11); only `parse_cli_overrides()` exists.
4. File source watching the parent directory with 100 ms coalescing (Section 10); the implementation polls mtime/size.
5. Metric export with the Section 12 names (`jk_reads_total`, `jk_snapshot_staleness_seconds`, ...); counters exist but nothing exports them.

## 5. Binding gaps

1. Python and Rust expose only the file source: no HTTP poll source, no managed generation source, no `read_view()`.
2. No `get_as`, no `_or_panic`, no Rust error enum, no Rust test guard.
3. No `.pyi` stubs, no wheel, no crate publication.
4. No Go or Java bindings (the spec mentions both), and no documented stable reader ABI for the shared snapshot.

## 6. Features worth considering

Proven useful in other configuration systems (Xapiand's own tooling, LaunchDarkly, Unleash, Configerator-style systems, etcd/Consul-backed setups), and noted on the website's Roadmap:

1. **Publishing CLI** (`detent set NAME VALUE`, `detent publish FILE`, `detent rollout NAME PCT`) so operators don't hand-write `curl` with version bookkeeping. The README's quick start is entirely `curl`.
2. **Audit history and rollback**: who changed what and when, list versions, revert to version N. The authority has revisions and receipts but no query surface for history.
3. **Dry-run validation** endpoint/flag that validates a snapshot or transaction without publishing.
4. **Metrics endpoint** (`/metrics`, OpenMetrics) on both daemons, plus a small exporter helper for client `Metrics`.
5. **Knob metadata**: owner, description, created/expires, and a stale-flag report. The kill-switch lifecycle relies on removal, but nothing helps find candidates.
6. **More value types**: string, float, JSON; and group overrides for int knobs.
7. **Targeting rules** beyond exact group names (attribute predicates, segments), and **scheduled changes** (apply at a time, or ramp on a schedule).
8. **Change notification API** for applications. `on_update` exists but is documented as the agent's hook, is exclusive with `read_source`, and isn't called for the initial snapshot.
9. **Optional bearer-token check on the classic distributor**, a cheap improvement for deployments that can't adopt the authority yet.
10. **Packaging**: Homebrew tap and container image (Xapiand ships both and its docs lead with them), Debian/RPM, FreeBSD port.
11. **CI** on Linux/macOS/FreeBSD with sanitizers, once 1.1 is fixed. The website's header shows the Pages build badge only, because there is no CI workflow to point at.
12. **Versioned schema for the authority health document**, so dashboards can depend on it.

## 7. Keeping the website in sync

The website lives in `website/` (Astro + Starlight, the same theme as the Xapiand docs) and deploys from `main` through `.github/workflows/pages.yml` whenever `website/**` changes. When changing user-visible behavior, please update the matching page:

| Change | Page |
| --- | --- |
| CLI flags or output | `website/src/content/docs/reference/cli/*.md` |
| HTTP endpoints, headers, status codes | `website/src/content/docs/reference/http-api/*.md` |
| Config fields, env vars, directories | `website/src/content/docs/reference/configuration.md` |
| Public C++ API | `website/src/content/docs/reference/cpp-api/*.md` |
| Limits | `website/src/content/docs/reference/limits.md` |
| Status of any item above | `website/src/content/docs/roadmap.md` |
