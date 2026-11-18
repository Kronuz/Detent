# connection-slow-reader-flake: Consensus CI: read_slowly and groups_upload stop failing on main

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status connection-slow-reader-flake`.

## Outcome

consensus main's CI is red on every recent commit from `read_slowly` in test/transport/connection_test.cc (asserts at lines 614, 688 and 693: `(stalled || held) && attempt < 3`, `!r.coarse_wakes || r.drained_expiries > 0`), on linux-tsan and macos-x86_64; and `consensus_transport_groups_upload` failed once on macos-x86_64 (run 44224758109). `groups_upload` lives in groups_test.cc, which transport-feed-scan owns and rewrites; it is out of scope here and goes to a follow-up after feed-scan lands. The quest names each cause and fixes the test's sizing or the code, whichever is wrong, without weakening what the test proves.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 Each failure's cause is named with a forcing that reproduces it on demand, failing 3 of 3 at main and passing at the fix: jobs.
- A2 `consensus_transport_connection` x200 at 4-way load passes with 0 failures: jobs.
- A3 If a cause is in product code, not the test, stop and report it to the lead before fixing: [manual] the journal says which it was.
- A4 The suite passes, plus CI green on every platform at the head.

## Contracts

None.

## Decisions

Filed by the lead 2026-11-17 after consensus main went red on these at 0ed76ef, 84814a9, 361d5d2 and aa7fa35.

## May land early

None.
