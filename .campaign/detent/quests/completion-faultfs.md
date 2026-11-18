# completion-faultfs: completion: the sync-seam crash model runs on faultlab's faultfs

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status completion-faultfs`.

## Outcome

D104's first move. completion's sync-seam tests (test/sync_seam_test.cc over test/seam_support.h) carry their own in-process crash model. After this quest they run on faultlab's faultfs (Kronuz/faultlab at faultlab:a452a8c, API fixed, over completion:d8b538c): one crash and media model, its recorder and event log, and completion's own model is deleted, with no shim. faultlab is a test-only dependency, pinned by commit.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 Every crash case the old model covered runs on faultfs with the same or stronger assertions; the journal maps each old case to its new one: the suite job.
- A2 completion's own crash model is gone: `git grep` finds no crash or tear state in test/seam_support.h or elsewhere in test/ outside faultfs calls: a job.
- A3 A seeded fault kills it: drop one sync in the seam (a mutant) and a faultfs case fails: a job.
- A4 faultlab's rules hold in the tests as written: a sync does not make mapped stores durable, no folds after the power cut, and two file versions only (append sizes not modeled); any case that needs more is listed in the journal, not faked.
- A5 The suite passes, plus CI.

## Contracts

Consumes faultlab:faultfs at faultlab:a452a8c. No completion API change.

## Decisions

Filed by the lead 2026-11-18 when faultlab-core landed (Xapiand lead). D104.

## May land early

None.
