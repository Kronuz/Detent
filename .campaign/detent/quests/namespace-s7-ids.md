# namespace-s7-ids: Agent store: derived ids, no storage identity

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s7-ids`.

## Outcome

Generation, segment, `.read` and `.owner` names are derived as D8.1 says and no longer carry a storage identity; every writer and reader uses them, still under the old `selected` descriptor. A rebuild at an unchanged head takes ordinal r + 1, so derived names never collide (D88).

## Acceptance criteria

- A1 No `StorageIdentity` remains in `.read`, `.owner` or segment ids; every user in src/detent_local and src/detent_testing derives them: a grep job and the suite
- A2 A rebuild at an unchanged head publishes under ordinal r + 1 and never reuses a name: a test that fails without it
- A3 A power cut at every storage primitive of publication still recovers to the last acknowledged generation or the one before: the crash model job

## Contracts

Consumes the agent store (`include/detent_local/agent_store.hh`, D8).

## Decisions

D8, D43, D88.

## May land early

Nothing: it changes names on disk.
