> Status: CURRENT (2026-11-02) Campaign plan after the handover from Codex; the release exits stay in [RELEASE_PLAN.md](RELEASE_PLAN.md).

# Detent campaign plan

Lead: the Claude Code session that took Detent over from Codex session `01a0f5d7` on 2026-11-02.

## Goal

Release Detent: the five exits of the [release checklist](RELEASE_PLAN.md#remaining-release-checklist)
(RC-1 integration, RC-2 client performance, RC-3 sustained capacity, RC-4 restoration, RC-5 the
composed release candidate), on the Kronuz libraries at their heads. The targets in
[SCALE_TARGETS.md](SCALE_TARGETS.md) stand: 1M knobs (5M qualified, 10M exploratory), 10k+
publishers, 250k+ agents, sub-millisecond warm reads.

## Rules shared with the Xapiand campaign

1. Stay on the tip. Every library pins its dependencies at their heads, and so does Detent. A stale
   pin is fixed in the library that holds it, never overridden by an earlier declaration here.
2. One implementation per mechanism. No legacy or duplicate APIs, shims or deprecated aliases.
3. Every library consumer is ours: change a library additively, or migrate every caller.
4. Coordinate with the Xapiand lead (`xapiand-74`) before changing a shared library's API, moving a
   pin wave or removing something. Its plan: `~/code/Kronuz/strata/docs/status/road-to-release.md`;
   its queue: `~/code/Kronuz/strata/docs/status/library-issues-2026-11.md`.
5. A library change is done only when CI is green on every job.

## Milestones

| Milestone | Content | Exit |
| --- | --- | --- |
| M1 On the tip | Every Kronuz pin at its head; Detent on coroutine HTTP handlers with no offload path | Done criteria below |
| M1b Async drivers | Coroutine drivers for journal and consensus (`task::task<T>`, injectable scheduler/clock/IO, bytes per idle group, a run-to-completion entry), then Detent's owner loops on them; agreed with Xapiand in [SHARED_LIBRARY_DECISIONS.md](SHARED_LIBRARY_DECISIONS.md#coroutine-drivers-for-journal-and-consensus-november-2) | Designs reviewed by the architect and the Xapiand lead; old Worker API deleted |
| M2 Integration (RC-1) | Re-qualify the parked candidates on the tip, one at a time: resources, shared HTTP clients, bounded MessagePack, persistent map, consensus transport, async HTTP log completion | Each lands or is rejected with evidence |
| M3 Client performance (RC-2) | First-read repeatability, incremental refresh, daemon startup attribution | Sub-millisecond resident-data first read held |
| M4 Capacity and restoration (RC-3, RC-4) | Ingestion ladder, sustained 5M, 10M attempt, fresh three-voter restore | Published operating envelope |
| M5 Release candidate (RC-5) | Platform and sanitizer matrix, install, service lifecycle, upgrade and rollback | Tagged release |

## M1: on the tip

Procedure: "Updating an application (for agents)" in
`~/code/Kronuz/cpp-libraries/docs/UPGRADING-2026-10.md`, step by step. Detent's pins are the
manifest `cmake/dependencies.json`. On 2026-11-02 all 33 Kronuz pins are behind their heads.

### Workstreams

| Workstream | Owner | Content |
| --- | --- | --- |
| W1 completion pins | lead | Move completion's own metrics and allocators pins to heads in the library; cascade completion through journal and consensus (and cluster where it pins them) |
| W2 HTTP coroutines | designer, then developer | Detent's three handlers (`detentd` Distributor, authority Service, relay Service) become `asio::awaitable<void>`; the worker offload, `should_offload`, `http::DeferredReply`, `Request::user_data` and blocking `future.wait_until` on the request path go; admission stays Detent's own (JSON refusals; http's `in_flight_limit` gate is set so it never fires), body caps map onto `set_max_body()` and `BodyStream`; reactor's new constructors |
| W3 pin waves | developer | Move pins dependency first in waves: leaves; then the completion chain; then reactor, http-parser, http, http-log, cluster with W2 |
| W4 threadpool and scheduler | Xapiand campaign, then W3 | ThreadPool's family is removed and scheduler runs on WorkPool; Detent takes threadpool, scheduler, logger and stash in one step when `xapiand-74` reports them landed |
| W5 evidence | lead | Input-class diffs, hot-path A/B with an A/A control, CHANGELOG |

### Critical path

```
W1 completion -> journal -> consensus pins ─┐
W2 design -> architect review -> implement ──┼─> W3 final wave + W4 -> full suite, sanitizers, CI -> W5 A/B -> land
W4 threadpool/scheduler (Xapiand) ──────────┘
```

W2 is the long pole: it is a concurrency change on every request path, so its design gets the
architect's preliminary review and its branch a code review before landing.

### Done criteria

1. Every Kronuz pin in `cmake/dependencies.json` equals its library's default-branch head at the
   move, outside every range in "Commits not to pin", and equal to what each library that declares
   the same dependency pins. A check in CI fails when the manifest differs from a declaring library.
2. completion, journal and consensus pin their dependencies at heads, each with CI green on every job.
3. No `workers` argument, `should_offload`, `DeferredReply`, `user_data`, `BodyReader` or blocking
   wait on a reactor thread remains in Detent. Every handler is a coroutine. The body cap is set
   from Detent's configured snapshot limit, so no accepted upload size changes.
4. The full native suite, the Python and Rust binding checks, ASan/UBSan and TSan (separate
   builds) pass; Detent's hosted CI is green on every job.
5. The input classes the guide names for Detent's libraries are diffed through the old and the new
   build: compressors' damaged input, http-parser's rejected requests, strict-stox, enum-reflection,
   random's sequence, logger output. Snapshot JSON, wire bytes and persisted images are identical.
6. The hot path is measured at the old and the new pins with `cpp-libraries/tools/ab_compare.py`
   and an A/A control, under `quiet strict` holds of at most ten minutes: `detent_read_bench`,
   the managed first read (sub-millisecond target held) and daemon publish and get.
7. `CHANGELOG.md` names each library moved (old pin, new pin), the sections acted on, the results
   that changed and the owner decisions taken.
8. `docs/DEPENDENCIES.md`, `docs/DEPENDENCY_UPGRADES.md` and the website pages the change touches
   are current.

## Parked candidates

Codex left these isolated, each built on the old pins. They wait for M1 and are re-qualified on
the tip in M2, never adopted on the old pins.

| Candidate | Branch or commit | State on 2026-11-02 |
| --- | --- | --- |
| Reader metadata capsule | `track/reader-capsule`, `track/resource-capsule` (`Detent:30fee44`) | Correct; real 5M first-read acceptance fails in three of four rows |
| Resource adoption | `.scratch/` worktrees | Functional gates pass; performance gate open |
| Shared HTTP clients | http qualification branch (`http:bd3e8e1`, not on http main) | Blocked on +13.2% caller CPU; built on the removed sync handler API |
| Bounded MessagePack | `msgpack:f8975e3`, `Detent:4c37ca6` | Functional; production-path cost open |
| Persistent map | `Kronuz/persistent-map` | Functional; update-cost outlier open |
| Scheduler wakeup fix | review re-check uncommitted on main | Upstream scheduler fixes the same missed wakeup (`scheduler:36afada`, `34631ec`); to confirm, then retire |

## Process

Pacing follows `pace claude advise`; on 2026-11-02 the week runs ahead (16% used against 7%
pace), so one token-heavy agent runs at a time and the lead does mechanical library work itself.
Builds and tests run under `quiet light`, timing under `quiet strict` holds of at most ten minutes.
