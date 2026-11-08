# client-spec: The client library, per the JustKnobs spec milestones 1-9

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status client-spec`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `justknobs-implementation-spec.md` section 14 (Build milestones 1-9), the first plan; the initial commit's message and tests record what was built.

## Outcome

An in-process C++ client for namespaced boolean and integer knobs: strict JSON snapshots over baked defaults, lock-free reads of one immutable snapshot per call, exact group, rollout and CLI precedence, a single updater that swaps atomically and keeps the last known good, with Python and Rust bindings. Distribution is HTTP (`detentd`), not the spec's optional gRPC source.

## Acceptance criteria

- A1 [artifact] Milestone 1, model, schema, defaults: both example files parse, every schema violation rejects the whole input, missing or invalid defaults fail initialization, a fuzz target runs without crash or unbounded allocation: `tests/detent/test_model_and_schema.cc`
- A2 [artifact] Milestone 2, instance read API: semantic matrix, golden hash vectors byte for byte, no call-site fallback API, zero allocation on successful non-rollout reads: `tests/detent/test_eval_semantics.cc`, `tests/detent/test_hash_golden.cc`
- A3 [artifact] Milestone 3, atomic publication: one snapshot per call, torture test and race detector pass, only complete snapshots observed, retired snapshots reclaimed: `tests/detent/test_concurrency_stress.cc`
- A4 [artifact] Milestone 4, process overrides and global adapter: unset override path is one load and branch, invalid overrides fail startup, overrides survive swaps and win precedence: `tests/detent/test_overrides.cc`
- A5 [artifact] Milestone 5, updater and file source: one swap per valid replacement; malformed, stale, partial and oversized updates never change state; watcher overflow rereads; reads continue through source failures: `tests/detent/test_file_source.cc`
- A6 [artifact] Milestone 6, observability and health: bounded counters, staleness, source health, structured logs, a diagnostics snapshot: `justknobs-implementation-spec.md`
- A7 [artifact] Milestone 7, test modes and Python: nested test guards restore state, a disable mode serves defaults plus CLI, Python exceptions and golden vectors match: `tests/detent/test_testing_guard.cc`, `bindings/python/tests/test_detent.py`
- A8 [artifact] Milestone 8, optional gRPC source: duplicate delivery harmless, bad checksum or oversize never parsed, resume after the accepted version, forced-disconnect tests: `justknobs-implementation-spec.md`
- A9 [artifact] Milestone 9, release readiness: every suite passes, dashboard and staleness alert exercised in staging, a kill-switch drill, public API and JSON schema versioned and documented: `justknobs-implementation-spec.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
