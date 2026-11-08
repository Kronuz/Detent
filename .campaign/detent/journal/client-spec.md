# client-spec: journal

State: landed
Quest: .campaign/detent/quests/client-spec.md (the lead's; never edit it)

History: landed at `Detent:f789380` on 2026-09-29, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | open: no fuzz target at this commit; the other three items are tested (`tests/detent/test_model_and_schema.cc`) |
| A2 | `tests/detent/test_eval_semantics.cc`: "Section 13 semantic matrix: bool default/group/rollout precedence", `tests/detent/test_hash_golden.cc`: "Golden hash-bucket vectors for the Section 8 stable-hash rule." |
| A3 | `tests/detent/test_concurrency_stress.cc`: "Section 13 concurrency torture test (Milestone 3 acceptance criteria)" |
| A4 | `tests/detent/test_overrides.cc`: "Section 11: CLI override parsing and precedence over both baked defaults" |
| A5 | open: the file source polls mtime, there is no watcher, so the overflow reread is not built (website roadmap, Client and Bindings 3) (`tests/detent/test_file_source.cc`) |
| A6 | open: not recorded at this commit; a metrics endpoint and a client-counter exporter are on the website roadmap (Operations 1) (`justknobs-implementation-spec.md`) |
| A7 | open: no disable mode at this commit (website roadmap, Client and Bindings 2) (`tests/detent/test_testing_guard.cc`, `bindings/python/tests/test_detent.py`) |
| A8 | open: not built: HTTP distribution (`detentd`) replaced it, and Stages 4-5 replaced that (`justknobs-implementation-spec.md`) |
| A9 | open: release readiness is Stage 6's exits RC-1 to RC-5 (`justknobs-implementation-spec.md`) |

## Result

Landed as `Detent:f789380` on 2026-09-29, "implemented per justknobs-implementation-spec.md". The commit records 14 ctest suites (schema, golden SHA-256 vectors, precedence matrix, overrides, file source, TestGuard nesting, a 64-thread 20,000-swap stress), four `detentd` suites including a three-node Raft test, Python and Rust binding suites, and clean ASan/UBSan and TSan runs on Linux; zero-allocation reads were checked by a scratch counting allocator that was not kept. No review record. Docs: `README.md`, `QUICKSTART.md`, `src/detentd/CLUSTERING.md`.
Left open: A1, A5, A6, A7, A8, A9.

## Rulings I made

None recorded.
