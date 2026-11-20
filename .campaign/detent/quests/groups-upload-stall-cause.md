# groups-upload-stall-cause: groups_upload loop stall on macos-x86_64: name the cause from a captured CI failure, then fix or bound it

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status groups-upload-stall-cause`.

## Outcome

consensus_transport_groups_upload fails intermittently on CI macos-x86_64 (D116's standing --no-ci). groups-upload-loop-stall landed a capture that records raw kernel and symbolicated user frames of a loop stuck in state U. When this quest lands, the cause is named from a captured failure and fixed in the product or bounded for the runner, and D116's exception for groups_upload is retired.

## Acceptance criteria

- A1 [manual] The capture's output from a CI failure is quoted in the journal (run id), symbolicated offline, and it says whether the loop was not scheduled (runner) or was in a handler that did not return (product).
- A2 A product cause is fixed with a test that fails without it; a runner cause gets a bound from the measured stall that stated headroom covers, with the derivation in the journal: jobs.
- A3 [decision] D116's groups_upload exception is retired or restated in PLAN.md.
- A4 The suite passes, plus CI: jobs.

## Contracts

None, unless the cause is in the transport: then include/consensus/transport/* or src/transport/*, which the lead grants.

## Decisions

Starts when a CI failure carries a capture (the lead watches).

## May land early

None.
