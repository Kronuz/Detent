# journal-host-ranges-race: journal: group_io and another loop's publish race on Host::ranges_

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-host-ranges-race`.

## Outcome

`Host::group_io` mutates `ranges_` on one loop's thread while another loop's `commit` -> `publish` reads it, with no lock. TSan reports it on journal main (job 261118-144636-e96deb: erase_if/emplace at host.h:294 against publish) and at journal-group-io-sweep's head. After this quest every access to `ranges_` is ordered (one owner loop with handoff, or a lock whose hold is bounded), and `journal_host_test` under TSan reports nothing for a test that drives group_io and publish from two loops concurrently.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 A test drives group_io on one loop and commit/publish on another concurrently; under TSan it reports the race without the fix (--fails-without) and passes with it: jobs.
- A2 The ownership rule for `ranges_` (which thread, which lock) is stated where host.h declares it, and in a mechanism note: [artifact] `docs/mechanisms/host-ranges.md`.
- A3 group_io per call stays within 1.2x of journal-group-io-sweep's after table at 1K and 1M ids (exclusive): jobs.
- A4 The suite, TSan and CI pass.

## Contracts

None: Host's API is unchanged.

## Decisions

Filed 2026-11-18 from journal-group-io-sweep's review F3 (pre-existing). High risk: concurrency.

## May land early

The A1 test, disabled: no.
