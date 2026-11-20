# journal-idle-groups: journal

State: working
Quest: .campaign/detent/quests/journal-idle-groups.md (the lead's; never edit it)

## Doing

Step 1 of the design's first buildable steps: TurnDriver::before_turn, deferred create_group and
reopen_group, one batched slot commit before each turn with transitions.

## Next

1. before_turn hook + deferred create/reopen (D5); fix host_test failed_reopen to fail-stop
2. TurnLog::peek, Host::park(loop) via the close path; host_test park_count (A2)
3. Streaming re-partition (D6)
4. host_test idle_crash and idle_damage with mutants (A3)

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | journal docs/design/idle-groups.md at journal:177814e |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

Coordinator's design inputs (review F2, F3, F5 of Xapiand turn-log-parity-design.md): answered in
idle-groups.md D1 (F3: parked is closed, a durable state recovery already exempts), D5 (F3:
unpark committed before the turn), D7 (F2), D8 (F5).

## Result

<pending>

## Rulings I made

- R1. Parked is the existing closed state (D1), not a new table state: same bytes, same exemption.
- R2. turn-log-parity's one-log-per-device shape is not on journal main: designed against its quest
  file; assumptions in idle-groups.md "Assumptions about turn-log-parity".
- R3. CMakeLists.txt is outside the file set: new cases go in host_test.cc (registered whole);
  the 1M scale comes from JOURNAL_PARKED_GROUPS in the job, a smaller default in ctest.
