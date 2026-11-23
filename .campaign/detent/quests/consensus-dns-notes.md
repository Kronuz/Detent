# consensus-dns-notes: consensus resolve: emplace inside the try, exit-wait CHANGELOG, fork note

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-dns-notes`.

## Outcome

consensus-dns-threads' review left N2, N4 and N5 open (consensus-dns-threads journal:85): `running.emplace` sits outside the try in `resolve.h` (about :208), so a throwing emplace leaks a running resolver; the CHANGELOG does not say exit waits for running resolvers; `docs/mechanisms/peer-dns.md` has no note on fork. After this quest the emplace is exception-safe and both notes are written.

## Acceptance criteria

- A1 A throwing emplace leaves no resolver running and no thread unjoined: a test that fails without the move (--fails-without): jobs.
- A2 [artifact] The exit-waits-for-resolvers line and the fork note: `CHANGELOG.md`, `docs/mechanisms/peer-dns.md`.
- A3 The suite and TSan pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J3).
