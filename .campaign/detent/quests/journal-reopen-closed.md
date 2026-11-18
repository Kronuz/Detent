# journal-reopen-closed: journal: a closed id recovers in the live Host

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-reopen-closed`.

## Outcome

Today an id handed to a Store by `recover_shared` (or created) can't be recovered or created again in the same live Host until the Host reopens (found by consensus-close-fenced's review). Xapiand evicts idle indexes and reopens them in the same process, up to millions of indexes per host, so this blocks its core workload. After this quest, closing a Store releases its id: a cleanly closed id recovers in the live Host, and a fenced one recovers once its Store has closed. Two live Stores never hold one id at once.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 Close then recover the same id in one live Host recovers exactly the committed records (clean close and fenced close, each with records staged but unsynced at close): a test that fails at main.
- A2 Two live Stores never hold one id: recover or create of an id whose Store is still open is refused, including while that Store's close is in flight (a close racing a recover): tests, TSan.
- A3 A crash at any point of a close-then-recover cycle recovers a state the old rule allowed (no record lost that was durable, none resurrected that was dropped): a seeded crash sweep job.
- A4 Churn: 10,000 close/recover cycles of 1,000 ids in one Host hold the Host's memory flat (bytes before and after reported): a job.
- A7 A Store that can't close into the table (fenced, or batches not covered by a checkpoint) can release its id: the Host drops that id's in-memory state (turn-log sequence, index entries, shard and opened nodes) while retention keeps every durable record, and recover in the live Host rebuilds it exactly: tests, plus heap per released id reported at 10,000 ids (the lead's target: the 2,398 B consensus-close-fenced measured goes to near zero).
- A5 The mechanism doc states the id lifetime rule: `docs/mechanisms/...`.
- A6 The suite passes, plus CI.

## Contracts

journal Host: an id is held from recover_shared or create until its Store closes; consensus's Driver and Xapiand's pool rely on it.

## Decisions

Filed by the lead 2026-11-18 at the Xapiand lead's request (cheap-reopen, pool). Reopen cases go in test/journal_test.cc because journal-faultfs holds CMakeLists and the host tests.

## May land early

None.
