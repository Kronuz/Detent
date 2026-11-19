# fixtures-port-zero: daemon fixtures bind port 0 and read the bound port

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status fixtures-port-zero`.

## Outcome

Fixtures reserve ports and release them before the daemon binds, a race with any other process (PLAN Not quests yet 5; namespace-s8-wiring journal:158-162, D5, D8): `authority.py:149` (`release_ports`), `managed.py:151` (reservation socket), `test_relay_service.py:168` (`free_port`), `test_dual_stack.py:39`. D5 and D8 went to namespace-followups, whose A1-A4 omit them. After this quest every daemon fixture starts on port 0 and reads the bound port from the daemon.

## Acceptance criteria

- A1 No fixture reserves a port (`git grep release_ports\|free_port` empty in tests) and the system tests pass: jobs.
- A2 The system tests pass 10 runs in a row under `-j` with another process churning ports: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U10).
