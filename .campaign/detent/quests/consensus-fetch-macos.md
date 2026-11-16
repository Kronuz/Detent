# consensus-fetch-macos: fetch_alongside_consensus fails on macos-x86_64 CI

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-fetch-macos`.

## Outcome

`consensus_transport_peers`'s `fetch_alongside_consensus` (F2 of consensus-fetch-lane) passes on macos-x86_64 CI, the slow runner, with its cause found: it fails there at three different checks (peers_test.cc:1155 on main a95847e; :1102, a commit past 2 s, and :1128, a fetch returning an error or wrong bytes, at consensus:771e1bb), while every other platform, TSan included, is green. A fetch error under load is a defect, not a timing bound to loosen.

## Acceptance criteria

- A1 The cause of each failing check is named in the journal with a reproduction: a local job that fails at the parent under the CI's conditions (e.g. a CPU hog or `taskpolicy -b`, repeated runs)
- A2 The fix makes that reproduction pass, and a fetch under load never returns an error or wrong bytes: the same job at the fix, plus the test
- A3 macos-x86_64 CI passes `consensus_transport_peers` on three consecutive runs at the fix: three CI runs named in the journal [manual]
- A4 The suite and TSan stay green: the suite check and ci

## Contracts

No API change.

## Decisions

Lead, 2026-11-16: consensus-late-append landed with --no-ci over this failure, which predates it. If a bound is loosened, the journal shows why it is a bound and not a defect.

## May land early

Nothing.
