# consensus-faultlab-repin: consensus pins faultlab at 5d4df9b (stale at_mark fix)

Risk: low (test dependency pin). The lead writes this quest; contributors read it and never edit it.

## Outcome

faultlab:5d4df9b (Xapiand msg-277) clears the at_mark records of an ordering point that a failing
directory sync removes; before it, a later point's crash could restore stale bytes, so crash tests
that inject failing directory syncs may have read stale state. consensus pins faultlab a3b71d1; this moves
it to 5d4df9b (also brings faultfs.h's split into its parts and the MemoryIO fixture).

## Acceptance criteria

- A1 The suite passes at the new pin, the crash and power-cut tests included: a job
- A2 `ci:ci` is green at the landed commit

## Contracts

None.

## Decisions

Lead, 2026-11-23: from Xapiand's msg-277.

## May land early

None.
