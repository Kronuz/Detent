# consensus-batch-turn-room: consensus worker: validate a batch against the host's turn room

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-batch-turn-room`.

## Outcome

`validate_configuration` (`worker.h:645`) does not check a batch against the journal host's turn room, and journal-turn-bound splits only multi-record turns, so a single oversized batch reaches the journal unchecked (reviews/consensus-wave-open-correctness.md:109 F6; journal is the other library involved). After this quest a configuration whose batch exceeds the turn room is refused at validation with a clear error.

## Acceptance criteria

- A1 A configuration whose batch exceeds the host's turn room is refused by `validate_configuration`: a test that fails without the check (--fails-without): jobs.
- A2 The suite and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (R7).
