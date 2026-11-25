# consensus-next-deadline-removal: Core::next_deadline() omits a due D3 removal, so a driver that sleeps to it delays the removal

Risk: high (consensus Core timing, membership). The lead writes this quest; contributors read it and
never edit it. Its machine state is in `state.json`: `campaign status consensus-next-deadline-removal`.

## Outcome

Found by consensus-liveness-asan-timeout (2026-11-24), which holds Ticks that land before
`Core::next_deadline()`: a due D3 removal waits for a Tick, but `next_deadline()` does not report
it. A driver that sleeps until `next_deadline()` (the Driver and any embedding that schedules by it)
therefore delays the removal until some other deadline or event. Make `next_deadline()` report every
deadline Core acts on at a Tick, the removal included, so sleeping to it never delays Core's work.

## Acceptance criteria

- A1 A deterministic test: a node with a due D3 removal and no other deadline; `next_deadline()`
  names the removal's time, and a Tick at that time performs it, by value. `--fails-without` at main.
- A2 An audit, as a table in `docs/mechanisms/deadlines.md`: every action Core takes on a Tick and
  the deadline that `next_deadline()` reports for it; any other omission found gets a test like A1.
- A3 The liveness harness's held-Tick mode (consensus-liveness-asan-timeout) gives traces identical
  to stepping every Tick, in all four profiles, with the removal case included: a job.
- A4 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

`Core::next_deadline()`'s meaning: the earliest time a Tick changes Core's state. No signature change.

## Decisions

Lead, 2026-11-24: its own quest, after consensus-liveness-asan-timeout, whose held-Tick harness
found it and proves the fix.

## May land early

Not applicable.

## Tasks

None.
