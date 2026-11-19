# groups-upload-loop-stall: groups_upload: name and fix the 1.3 s connection-loop stall from a probed CI failure

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status groups-upload-loop-stall`.

## Outcome

groups-upload-3s-class named the unexplained groups_upload class: host 1's connection loop did not run for about 1.3 s of a 1,525 ms gap (CI 44443844218, macos-x86_64), with both sockets empty and windows open. Its per-loop probe (consensus:84608cc) prints each loop's stamp age, CPU time, run state and stack once stuck. This quest waits for a CI failure with that probe, names runner or product from it, and fixes a product cause.

## Acceptance criteria

- A1 [manual] The probe's output from a CI failure is quoted in the journal (run id), and it says whether the loop was not scheduled (runner) or was in a handler that did not return (product).
- A2 A product cause is fixed with a test that fails without it; a runner cause gets a bound from the measured stall that stated headroom covers, with the derivation in the journal: jobs.
- A3 The suite passes, plus CI.

## Notes

- Start when a probed failure appears: sweep consensus CI failures of groups_upload after consensus:84608cc lands.
- A product fix in the transport needs include/consensus/transport/* or src/transport/*: the lead grants them.
- The same class hit consensus_transport_groups_reader on macos-x86_64 (CI 44467383652, group 2 longest gap 2600 ms; excerpt .scratch/groups-reader-44467383652.txt): its fix covers groups_reader too.
