# consensus-recovered-over-limit-start: a recovered group over its control headroom starts or fails loudly

Risk: high (liveness at recovery). The lead writes this quest; contributors read it and never edit it.
Its machine state is in `state.json`: `campaign status consensus-recovered-over-limit-start`.

## Outcome

Found by consensus-driver-pressure-rearm (journal, its table's "initialization" row): when a group
recovers with its store's `used` above `hard - control_pool`, the pre-Start Tick is refused with
Pressure, so the group never starts. On main nothing wakes it; after consensus-driver-pressure-rearm
it polls every `retry_ns`, which cannot clear a pool that only the group's own work releases. After
this quest such a group either starts (its control headroom is reserved so the first control append
always fits, or recovery frees what it can first), or the open fails with a named status the caller
sees, never a silent stall.

## Acceptance criteria

- A1 First a table: every way recovery can leave a group's store above its control headroom, and
  what frees space (compaction, retirement, a snapshot) and whether it needs a started group: in the journal
- A2 A deterministic test recovers a group whose store is above `hard - control_pool`: at the fix it
  starts (or the open fails with the named status), by value; at main it never starts within the
  test's bound (`--fails-without`): jobs
- A3 [artifact] `docs/mechanisms/deadlines.md` or the admission doc states the rule
- A4 The suite passes and `ci:ci` is green at the landed commit

## Contracts

Changes when a recovered group starts; no format change. If the right answer narrows what a caller
can open (a store that opened before now refuses), that is the owner's call: stop and ask.

## May land early

None.
