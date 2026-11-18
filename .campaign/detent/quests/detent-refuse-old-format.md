# detent-refuse-old-format: Detent: refuse an old journal format at startup with a named error

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-refuse-old-format`.

## Outcome

On a journal written before a format change (dense-sync-stack's probe 261118-064115-2e8717: the 2026-10-23 10K fixture after Detent:53abd637), all three detentd voters log "authority recovery application fenced" (service.cc:1983) and hang with no leader. Detent keeps no backward compatibility, so the old format is not to be read, but it must be refused loudly: startup names the format it found and the one it expects, and exits nonzero.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 A voter started on a journal of an older format exits nonzero within seconds with a named error naming both formats, never fencing and waiting: a test that fails without the fix.
- A2 The suite passes, plus CI.

## Contracts

None.

## Decisions

Filed 2026-11-18 from dense-sync-stack's main-arm diagnosis; greenfield (no compat) means refuse, not migrate.

## May land early

None.
