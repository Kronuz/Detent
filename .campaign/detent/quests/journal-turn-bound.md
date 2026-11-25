# journal-turn-bound: Journal: a turn never exceeds a segment; a wave of opens commits

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-turn-bound`.

## Outcome

`TurnDriver::start()` packs every pending record into one turn with no segment bound. When 64 groups open at once, their create records overflow a 16 KiB segment, `TurnLog::encode` throws "turn exceeds a segment", the driver fails every staged operation, and each consensus store reads as fenced (consensus-wave-open's probes 261117-191119-28bc6a and -191129-a23d83: 64 at once fails 64 of 64 at journal 7c08540 and cc95ba6; waves of 8, 16 and 32 pass). After this quest a turn takes only the records that fit a segment and the rest go in the next turn, so any wave commits; a single record larger than a segment fails only its own operation.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 A wave of pending records larger than one segment commits across several turns, every record durable and in order: a test, failing at main with "turn exceeds a segment" (`--fails-without`).
- A2 A single record larger than a segment fails only its own operation; the driver stays healthy and later records commit: a test.
- A3 The crash-point suites keep their outcomes, including a crash between two turns of one wave: the suite and a crash-model case.
- A4 [manual] consensus-wave-open's 64-at-once probe passes against this branch: the lead reads the consensus job pinning this commit (it runs in consensus, so it cannot be a journal job).
- A5 The suite passes, plus CI.

## Contracts

No format change: turns keep their bytes; only how many records one turn takes changes.

## Decisions

D114 (lead). The cause was found by consensus-wave-open; its journal has the analysis.

## May land early

None.
