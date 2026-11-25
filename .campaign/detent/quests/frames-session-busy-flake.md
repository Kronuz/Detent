# frames-session-busy-flake: test_authority_frames_service gets 409 session_busy on macOS CI

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status frames-session-busy-flake`.

## Outcome

test_authority_frames_service failed once on main's macos-x86_64 CI (run 44559384726, Detent:8f48e4ba) with `frames status 409: {"error":"session_busy"}` at 4.74 s; no earlier run in the last 40 shows it. session_busy means a second frames request reached a session whose previous request was still open (service.cc:2243, 2645). Find whether the test asks again before its previous reply settled (a test race) or the service holds a session busy after its reply was sent (a service defect), and fix the cause, not with a retry.

## Acceptance criteria

- A1 The cause is named in the journal with the line that produces it, shown by a reproduction that fails at main (a delay injected at the named point, or a loop of the test under load): jobs.
- A2 The fix passes 200 runs of the test under light load and the reproduction from A1, which must fail without the fix (`--fails-without`): jobs.
- A3 The suite and CI pass: jobs.

## Notes

Filed 2026-11-19 from main's CI. Ordered after loop-idle-waits, which changes the authority's idle waits in service.cc; reproduce at the merged code.
