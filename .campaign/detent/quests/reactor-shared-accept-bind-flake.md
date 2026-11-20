# reactor-shared-accept-bind-flake: reactor runtime test: shared-accept retirement bind hits EADDRINUSE on macos-arm64

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reactor-shared-accept-bind-flake`.

## Outcome

The `reactor` runtime test (test/test.cc) never aborts on `bind: Address already in use` in the shared-accept retirement case. Seen on CI macos-arm64 at reactor:fbad85f (run 44650606033): an uncaught `std::system_error` from bind ended the process after ten "shared accept destination reached before retirement" checks. The cause is named (port reuse between iterations, TIME_WAIT, an ephemeral-port race, or a real retirement defect) before any fix.

## Acceptance criteria

- A1 The cause is named from a reproduction (a loop of the case under load, locally or on a macOS runner), with the failing iteration's bound port and socket state: jobs.
- A2 The fix makes the case pass 200 iterations under load on macOS, and a mutant restoring the old binding reproduces the abort (--fails-without): jobs.
- A3 The suite and CI pass: jobs.

## Contracts

None, if the cause is in the test. If it is a reactor defect, stop and report: the quest's risk and file set change.

## Decisions

No retry or catch that hides the error: the lead.

## May land early

Everything.
