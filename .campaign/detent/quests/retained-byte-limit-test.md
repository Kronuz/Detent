# retained-byte-limit-test: test that recovery is refused below the stored history's bytes

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status retained-byte-limit-test`.

## Outcome

retained-state's review CR2 (`docs/reviews/retained-state.md:45`) asks for a test that recovery is refused when `retained.log_bytes` is below the stored history; `test_node.cc` tests only the entry-count limit. After this quest the byte limit has its own refusal test.

## Acceptance criteria

- A1 Recovery with `retained.log_bytes` below the stored history is refused: a test that fails at a mutant ignoring the byte limit (--fails-without): jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U15).
