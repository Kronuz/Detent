# journal-drop-memory-io: journal: delete memory_io.h

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-drop-memory-io`.

## Outcome

D104 step 8: include/journal/memory_io.h is deleted from journal, with its mentions in AGENTS.md, ARCHITECTURE.md and docs; nothing in journal, consensus or Detent includes it.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 `git grep memory_io` over journal and consensus main finds nothing; the suite passes, plus CI: jobs.

## Contracts

None.

## Decisions

Filed 2026-11-18; needs consensus-faultfs. Greenfield: no shim.

## May land early

None.
