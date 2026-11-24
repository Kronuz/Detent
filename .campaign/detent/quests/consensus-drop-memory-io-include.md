# consensus-drop-memory-io-include: consensus: drop driver_install_request_test's unused memory_io include

Risk: low (one test include). The lead writes this quest; contributors read it and never edit it.
Its machine state is in `state.json`: `campaign status consensus-drop-memory-io-include`.

## Outcome

`test/driver_install_request_test.cc:40` includes `journal/memory_io.h` and uses nothing from it.
journal-drop-memory-io deletes that header, and its A1 needs `git grep memory_io` over consensus
main to find nothing. The include goes.

## Acceptance criteria

- A1 `git grep memory_io` at the landed commit finds nothing, and `ci:ci` is green there.

## Contracts

None.

## Decisions

Lead, 2026-11-23: its own one-line quest, since no active quest holds the file.

## May land early

Not applicable.

## Tasks

None.
