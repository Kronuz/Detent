# consensus-driver-pressure-rearm: a Tick refused with Pressure re-arms with a back-off, not at Core's past clock

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
is in `state.json`: `campaign status consensus-driver-pressure-rearm`.

## Outcome

Found by consensus-next-deadline-removal's correctness review (R5,
`.campaign/detent/reviews/consensus-next-deadline-removal-correctness.md`) and shown on main by job
261124-…-a3e432: when the Worker refuses a due Tick with Pressure, `Driver::arm()` re-arms at Core's
past clock (driver.h:832-846), so a due removal or a follower's past election deadline under
control-pool pressure is re-stepped on every wheel pass until the pressure clears. After this
quest a refused Tick re-arms once the pressure can have cleared (the pool's release wakes it, or a
bounded back-off), never in the past, and no deadline Core acts on is lost.

## Acceptance criteria

- A1 A deterministic test: a Driver whose Worker refuses due Ticks with Pressure for a held
  interval counts its re-steps, by value: bounded at the fix, one per wheel pass at main
  (`--fails-without`): jobs
- A2 When the pressure clears, the refused Tick runs within one wheel pass and its action (the
  removal, the election) happens, by value: a job
- A3 [artifact] `docs/mechanisms/deadlines.md` states the Driver's re-arm rule under Pressure
- A4 The suite passes and `ci:ci` is green at the landed commit

## Contracts

Driver scheduling only; Core's decisions and `next_deadline()` are unchanged.

## May land early

None.
