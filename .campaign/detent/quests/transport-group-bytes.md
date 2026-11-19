# transport-group-bytes: consensus host transport: per-group bytes, vector FIFO, one GroupTable lookup, owe order

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-group-bytes`.

## Outcome

consensus's host transport keeps 104 B per idle group against D108's 64 B budget and queues outgoing frames in a `std::deque` (`host_transport.h:378`, about 330 MB per host at 10K in-flight groups; transport-feed-scan journal:260-267, 288), and each control frame does two `GroupTable` lookups (+17-20%, D115). `owe` (`host_transport.h:479-502`) puts owed credit ahead of control frames (reviews/transport-feed-scan-correctness.md N2, a defect) and keeps the repaid prefix (N3), and `lane.closes` is uncapped (reviews/transport-identity-security.md N1). After this quest control frames go first, an idle group fits 64 B through `open_of(group)`, the FIFO is a vector, a control frame costs one lookup, and `lane.closes` is bounded; N1 and N4 of the feed-scan review get a written disposition.

## Acceptance criteria

- A1 Control frames leave before owed credit and the repaid prefix is released (N2, N3): a test that fails without the fix (--fails-without): jobs.
- A2 An idle group is at most 64 B (D108) and `lane.closes` stays bounded under a close flood that grows it without the cap (--fails-without): jobs.
- A3 Per-host bytes at 10K in-flight groups and per-control-frame cost, before and after at named commits (transport_idle bench, strict): jobs.
- A4 The suite, TSan and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J1, U8, J2, R2).
