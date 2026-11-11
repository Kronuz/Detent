# namespace-s7: journal

State: working
Quest: .campaign/detent/quests/namespace-s7.md (the lead's; never edit it)

## Doing

Evidence jobs at the head: the native suite and pins (A1-A4 tests), the crash-model job (A3) and the
N6 and unsynced-reuse mutants (A3, fails-without). Branch pushed for CI.

## Next

1. Paste `campaign brief`'s Evidence table when the jobs finish; set State to ready for review.

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
