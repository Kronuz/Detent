# pack-scrub-hash-count: PackScrub counts what it hashes

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status pack-scrub-hash-count`.

## Outcome

PackScrub hashes without `count_hash`, while `coordinator.hh:85` says the stats cover everything hashed (namespace-s4 journal:36, F2). After this quest PackScrub's hashing is counted, so the stats match the comment.

## Acceptance criteria

- A1 A scrub's hashed bytes appear in the hash counts: a test that fails without the count (--fails-without): jobs.
- A2 The suite passes: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J13).
