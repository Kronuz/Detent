# old-flakes-disposition: reproduce or retire ledger flakes 3, 4, 5, 8 and the catch-up segfault

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status old-flakes-disposition`.

## Outcome

N2 shows done in MAP, but its flake dispositions are owed (PLAN Not quests yet 1; `docs/status/http-coroutines.md:114-118`; `docs/DEPENDENCY_UPGRADES.md:258-262`): flake 3 (`managed.py:274`, agent did not converge), 4 (`test_transport_https.cc:76`), 5 and 8 (`test_local_paged_publication_artifacts` StoragePressure, TSan), and the catch-up segfault under `-j`; ci-test-flakes covered a different set. Also `test_managed_process.sh:147-151` prints nothing when the agent-empty wait fails (ci-test-flakes journal:109). After this quest each flake is reproduced and fixed, or retired with a measured zero rate, and the wait prints its state on failure.

## Acceptance criteria

- A1 Each flake run 50 times at main (TSan where it was seen): rate per flake: jobs.
- A2 A reproduced flake is fixed with a test that fails without the fix (--fails-without): jobs.
- A3 [artifact] The disposition of each in the ledger: `docs/DEPENDENCY_UPGRADES.md`.
- A4 A failed agent-empty wait prints what it saw: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U9, J14).
