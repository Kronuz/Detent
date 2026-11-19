# transport-receive-bound: consensus host transport: bound a receiver's per-loop batches

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-receive-bound`.

## Outcome

`arrived()` (`host_transport.h:678-722`) appends a receiver's per-loop batches without bound, so a peer that ignores the credit window or floods control frames grows memory without limit (reviews/transport-groups-correctness.md:133 N2, assigned to transport-identity or transport-admission and taken by neither). After this quest a receiver holds at most one credit window of data frames and a fixed number of control frames per group per loop, and refuses or closes past them.

## Acceptance criteria

- A1 A peer sending past its credit window is refused or closed, and the receiver's buffered bytes stay at the window: a test that fails without the bound (--fails-without): jobs.
- A2 A control-frame flood is bounded the same way: a test that fails without the bound (--fails-without): jobs.
- A3 The suite, TSan and CI pass, and the transport_idle bench stays within 5% of main (strict, before and after): jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (R1).
