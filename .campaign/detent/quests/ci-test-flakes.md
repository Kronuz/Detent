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
- A4 `test_local_paged_publication_artifacts` passes linux-tsan: it failed main 5 of 73 runs (Detent:39b5314a, ea3d591e, 06fa2a03, c02504e9, ce7faa41). Likely cause (deps-workpool-repin journal, probe 261116-195652-960b07): `PagedDeltaPlanBuilder::take()` retires the Work owner on the RetirementRuntime thread after the test's `limit - used` pressure admission, so `rejects<StoragePressure>` at line 69 sees freed headroom; wait for the domain's residents to settle before admitting, as test_retirement_factory.cc:77 does. A TSan job running it 100 times passes, and a mutant that admits before settling fails it
- A5 `test_system_managed_all-down` failed 1 of 200 TSan runs: find the cause or show it gone over 200 runs, and make CI upload its member logs (they land in `<repo>/.scratch/stage6/`, which `failure_artifacts` does not match): a job and a CI artifact path
- A6 `test_delivery_repair_scrub-worker` fails on main (job 261117-005929-a909be, `--repeat until-fail:8`): the scrub-carry hashing-count check at `tests/detent_delivery/test_repair.cc:499` saw 2,561,074 bytes hashed after the delta against a new root of 2,752,016. Name the cause, fix it, and pass 200 repeats under load: jobs
- A7 consensus `peers_test.cc:1196` (the retried fetch after a frozen peer, `!retried->error && retried->bytes == ...`) fails on macos-x86_64 CI under load (transport-connection runs 2f6f210, 677c864; passed on rerun): name the cause in today's `Peers` fetch or the test, fix it or show consensus-transport's lane port supersedes it (D106): jobs

## Contracts

None, unless (3) changes the authority service's admission behavior; then name it here before building.

## Decisions

D54, D55.

## May land early

Each test's fix, once its A1 and A2 rows are met.
