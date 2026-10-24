# Reusable-library extraction checklist

**Updated:** 2026-10-23. This is the restart index for extraction work. The historical audit is [LIBRARY_BOUNDARIES.md](LIBRARY_BOUNDARIES.md); its original inventory and test counts are not current completion claims. Extraction and consumer adoption are separate gates.

## Required before release

| ID | Mechanism and destination | Evidence and remaining work | Standalone brief |
| --- | --- | --- | --- |
| EX-1 | Byte accounting, allocation adapters and deferred retirement → existing `allocators` optional targets | Shared resources are published. Detent still has private accounting and retirement bodies. Recover the isolated migration, reconcile current APIs and owner-lifetime rules, remove duplicate engines, measure footprint and qualify composed consumers. Disk settlement stays a domain policy. | [Resources](handoffs/resource-primitives.md) |
| EX-2 | Blocking and coroutine HTTP client machinery → existing `http`; common TLS → existing `reactor` | Isolated client/parser migration and held-session tests exist. Main still has private client implementations. Reconcile current shared APIs, preserve authentic transport settlement, qualify all clients and remove migrated parser/cache/deadline bodies. Existing TLS reuse is not a reason to extract another TLS layer. | [HTTP clients](handoffs/http-clients.md) |
| EX-3 | Bounded incremental MessagePack lexer → existing `msgpack`; encoding → its existing packer | Isolated migration and logical-byte fixtures exist. Main retains its private codec. Recover the candidate, reconcile current library API, complete production-path cost and platform checks, then adopt an immutable revision and delete the private codec engine. Schema validation stays local. | [Bounded codec](handoffs/bounded-codec.md) |
| EX-4 | Versioned ordered map → proposed `persistent-map`, subject to a fresh catalog check | The private standard-library-only AVL remains in authority. Qualify allocation, comparator and reclamation contracts before extracting. No library extraction/adoption is claimed. | [Persistent map](handoffs/persistent-map.md) |

Each item requires library tests and supported-platform CI, consumer integration, exact valid protocol/storage bytes, failure and lifetime tests, and measured CPU/memory costs. Published library code alone does not check the item off. Final consumer qualification uses immutable public sources without hidden local overrides. Do not combine dependency-only performance comparisons with extraction changes.

## Conditional candidates and local boundaries

1. **Daemon/service plumbing:** configuration directories, PID ownership, readiness and detach behavior may be shared with another daemon project. Extract only when a second consumer adopts an agreed contract; otherwise record the keep-local decision before release.
2. **Segment indexes and immutable storage utilities:** revisit after SC-2/3 fixes establish a sound generic mechanism. Candidate pieces include budgeted ordering and immutable directory lookup. Publication policy, generation identity, leases and durable proofs stay with their owners. No generic storage framework is required merely to clear the release gate.
3. **Task scheduling, transfer helpers, mapped-file handling and filesystem trust:** evaluate existing scheduler, completion, transport and resource facilities before adding code. These are reuse audits, not authorization for new overlapping libraries. Follow the consolidation responses and shared-library decisions.
4. **Application semantics:** knobs, rollout hashing policy, scoped authorization, publication windows, delivery wire schemas and session promises stay in Detent. A library may own a generic primitive beneath them without acquiring those policies.

The metrics/completion/journal/consensus split and shared peer transport/TLS work have recorded progress in the historical audit. Do not repeat those splits. Check current pins and remaining wrappers before changing a boundary.

## Resume from a clean session

1. Read `AGENTS.md`, `docs/status/README.md`, this checklist, `docs/RELEASE_PLAN.md` and `docs/SHARED_LIBRARY_DECISIONS.md`. Check `git status`, current pins and active worktrees. Read the sibling catalog's `AGENTS.md`, including its no-duplication rules, before editing a shared library.
2. Check these existing worktrees and preserve uncommitted changes before rebasing or transplanting: `.scratch/library-adoption-20261017/detent-resource-migration`, `detent-http-client-extraction`, and `detent-msgpack-extraction` under the same parent. They are older candidates, not safe blind cherry-picks onto current main. Their committed HEAD alone does not identify uncommitted candidate work.
3. Recover component evidence from `RESOURCE_EXTRACTION_ARCHITECTURE.md`, `HTTP_CLIENT_EXTRACTION_ARCHITECTURE.md`, `MSGPACK_EXTRACTION_ARCHITECTURE.md`, and the `resource-extraction-*`, `http-client-preliminary-*`, and `msgpack-extraction-preliminary-*` reports in `benchmarks/`. Prefer these to rerunning broad discovery. Scratch artifacts may be absent in a new clone; committed evidence remains the starting point.
4. Work in order EX-1 → EX-2 → EX-3 → EX-4 unless a measured dependency requires another order. Finish one component through review and adoption before opening another structural implementation. Keep storage fixes and ingestion diagnostics coordinated with the [blocker handoffs](handoffs/README.md).
5. Run `quiet check` before builds/tests; use `quiet light` with at most four jobs, `quiet exclusive` for published comparisons. Work serially. Use one narrow independent review at a component boundary, not a review tree or per-commit approval cycle.
6. For each completed item, record public library revision, consumer commit, deleted private mechanism, test/platform results, raw before/after evidence and explicit exceptions here. Update the catalog and release plan. A conditional item closes with either adoption evidence or a reasoned keep-local decision.

**Next extraction step:** inspect and reconcile EX-1's existing migration against current `allocators`; do not rebuild the resource primitives from scratch. The separate SC-2/3, SC-4 and SC-5 blockers remain open and are not closed by extraction.
