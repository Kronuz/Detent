# journal-host-guards: journal: Host::close_group refuses a sequence below the last acknowledged record; wake() and a refused post_call

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-host-guards`.

## Outcome

journal's Host can't lose an acknowledged record through close, and `TurnDriver::wake()` never drops a wake silently. Both come from Xapiand's turn-seal durability review (landed at journal:555b69f).

## Acceptance criteria

- A1 `Host::close_group` (host.h:321-328 at journal:555b69f) refuses a close sequence below the group's last acknowledged record instead of forgetting the records after it, the same rule as TurnLog's supersede guard; recovery after a refused close keeps every acknowledged record: a test with values, a mutant without the check killed, and the crash sweep
- A2 `wake()` never ignores a refused `post_call`: a refusal is an error the caller sees (or the wake is retried on the driver's own loop), and the scheduler requirement for calling it from any thread is stated in the API doc; Detent's and consensus's schedulers used with it either provide a thread-safe `post_call` or wake from their own loop: a test of the refusal, TSan

## Contracts

Changes journal's Host and TurnDriver contracts; consumed by consensus-driver's Worker and Xapiand's strata.

## Decisions

Lead: Detent owns journal's Host (journal-driver), so the fix is Detent's; findings from the Xapiand lead, 2026-11-17.

## May land early

Tests.
