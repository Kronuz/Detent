# namespace-s7: journal

State: working
Quest: .campaign/detent/quests/namespace-s7.md (the lead's; never edit it)

## Doing

Writing the agent-store module: `include/detent_local/agent_store.hh`, `src/detent_local/agent_store.cc`
(D8: table codec, derived ids, linkat publication with verified reuse, startup barrier).

## Next

1. Module + unit tests (A1, A2) in `tests/detent_agent/test_agent_store.cc`.
2. Crash model (A3) in `tests/detent_agent/test_agent_store_crash.cc` over completion's sync seam,
   plus the N6 revert mutant.
3. Catch-up test (A4 and the absent-key tombstone) with the real paged builders.
4. docs/status/namespace-s7.md; suite job; push for CI.

## Blocked on

Nothing blocks the criteria. msg-21 to the lead: the file set has no src CMakeLists, so the
`detent_agent_store` library target lives in `tests/detent_agent/CMakeLists.txt`; wiring DiskStore,
coordinator and readers to the table is outside the set.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |

## Result

Pending.

## Rulings I made

- The agent store is a module beside DiskStore (its own directory), the `agent-store` contract S8
  and S9 consume; it carries generation files as bytes and verifies them by SHA-256.
