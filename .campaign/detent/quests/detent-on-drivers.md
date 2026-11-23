# detent-on-drivers: async-drivers 6: Detent's authority on the drivers and host transport

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-on-drivers`.

## Outcome

Detent's authority runs on the drivers, per `docs/design/async-drivers.md` (Detent) Build plan row 6: the owner-loop table, `AuthorityPeers` on the host transport, no 1 ms timer in the authority; journal and consensus pins move to the driver commits.

## Acceptance criteria

- A1 The owner-loop table and `AuthorityPeers` run on the drivers and the host transport, and the authority has no 1 ms timer: the native suite, a grep job and an idle-CPU job
- A2 CPU per request and p99 stay within the November 2 item 7 gate: before/after strict jobs at named commits (D135: strict, not exclusive)
- A3 The pins check passes with journal and consensus at their driver commits: the pins check

## Contracts

Consumes consensus-transport, consensus-driver, journal-driver.

## Decisions

D96, D97 (owner: Q2 Sink, Q3 one directory per host, Q4 quiescence quest, Q7 node identity). Xapiand cluster-release (Xapiand:15c3e05ac) orders after this quest by this id.

## May land early

Nothing.
