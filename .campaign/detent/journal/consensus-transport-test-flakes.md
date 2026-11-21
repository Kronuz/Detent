# consensus-transport-test-flakes: journal

State: working
Quest: .campaign/detent/quests/consensus-transport-test-flakes.md (the lead's; never edit it)

## Doing

Reproducing both flakes locally (macOS x86_64 native, plain and TSan builds in
~/.scratch/detent/consensus-transport-test-flakes) before any fix.

## Next

1. Fetch: time the 16 MiB reader (bytes_at on the loop) and the heartbeat gaps under TSan; decide.
2. group-full: loop it under load; name why 100 frames were delivered before the 4 s stall began.

## Blocked on

Nothing. Lead's msg: node-admission edits connection_test.cc, links_test.cc, CMakeLists.txt and lands
first; I leave them alone and merge consensus main after it lands.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

## Rulings I made
