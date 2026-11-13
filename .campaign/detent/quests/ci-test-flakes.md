# ci-test-flakes: Three CI test flakes: managed relay, writer failover, authority admission

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status ci-test-flakes`.

## Outcome

Three tests that failed once in CI today and passed on rerun pass every time, each fixed at its cause (test or product), never by a wider timeout:
1. `test_system_managed_relay` on macos-x86_64 (okt-followups' CI, 2026-11-12).
2. `test_system_authority_writer_failover` on linux-x86_64-clang (CI run 43355759936, namespace-measure).
3. `test_authority_service` on macos-x86_64, aborted on an uncaught `http::RequestRejected {"error":"admission_full"}` (CI run 43346413800 attempt 6, coordinator-idle-flake).
4. `test_authority_dns_voters_names` on native macOS: node-2 `bind: Address already in use` on a port picked free (job 261112-230929-531cef, namespace-s5-2 at b6161f57); a free-port race in `test_dns_voters.py`.
5. `test_local_paged_publication_artifacts`: "invalid paged artifact operation accepted", 4 of 400 under 40 concurrent copies at main 8d07ecad (job 261112-235559-3c947d); 0 of 60 serial.
6. `test_authority_snapshot_catchup` on native macOS: `test_multivoter.py:304` read of a voter response failed (job 261112-235321-9e6f06, namespace-s7-ids, which changes no authority code).
An uncaught product exception in a test (3) is a finding about the product until shown otherwise.

## Acceptance criteria

- A1 Each cause named in the journal with a reproduction forced by an interleaving, a delay injection or a resource limit (the slow macos-x86_64 runner's timing is the first suspect), failing before its fix: jobs, `--fails-without`
- A2 Each test passes 200 repeats under load at the fix: one light job per test
- A3 The suite passes: a job

## Contracts

None, unless (3) changes the authority service's admission behavior; then name it here before building.

## Decisions

D54, D55.

## May land early

Each test's fix, once its A1 and A2 rows are met.
