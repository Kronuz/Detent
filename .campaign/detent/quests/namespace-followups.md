# namespace-followups: Namespace review follow-ups: write-side bounds, dead relay disk fields, relay staging errors

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-followups`.

## Outcome

The non-blocking findings of the namespace-s5-3, namespace-s8 and namespace-s7-store reviews are closed: history enforces its derived file bounds when it writes, a count-cut seal survives a restart and a power cut, the relay reports no disk fields that nothing feeds, and a relay staging I/O error does not poison reads until restart.

## Acceptance criteria

- A1 A seal that would write a file above its kind's bound fails before writing (s5-3 review F8): a test plus a mutant that skips the check, killed
- A2 A restart and a power cut at every primitive of a count-cut seal recover byte-identically (s5-3 review F9): the crash-model job
- A3 The relay's `disk_*` status fields, which nothing feeds since namespace-s7-store, are removed with every reader (D43): a grep job and the suite
- A4 A staging I/O error on the relay (a full disk) fails that adoption and later reads recover without a restart (s8 review D3): a test

## Contracts

The relay status JSON loses its `disk_*` fields; no format change otherwise.

## Decisions

Collected by the lead (2026-11-13) from reviews namespace-s5-3-durability (F8, F9), namespace-s8-durability (D3) and namespace-s7-store's contributor report (dead relay disk fields).

## May land early

None.
