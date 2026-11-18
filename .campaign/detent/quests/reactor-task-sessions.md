# reactor-task-sessions: reactor sessions on task (step R)

Risk: high. History: this landed before the campaign, under the earlier campaign skill, at reactor:2fd30c8 (2026-11-02); its ledger `docs/status/task-sessions.md` in Kronuz/reactor is the record. The quest's repository is `../reactor`. Its machine state is in `state.json`: `campaign status reactor-task-sessions`.

## Outcome

reactor's `Session` is a `task::task`, each reactor owns one scheduler per loop, accepts and UDP receives await through `await_asio`, and shutdown runs as bounded phases (`stop()`, `close()`, `destroy_roots()`), each running the earlier ones, with no extra allocation per accepted connection.

## Acceptance criteria

Carried from the ledger, each shown by the ledger as an artifact.

- A1 [artifact] T1 one shape: no `asio::awaitable`, `use_awaitable`, `awaitable_operators` or `co_spawn` outside the bench comparator: `docs/status/task-sessions.md`
- A2 [artifact] T2 suites: head's nine plus `reactor_session` and `reactor_alloc`, all pass: `docs/status/task-sessions.md`
- A3 [artifact] T3 probes on Linux under ASan+UBSan and TSan: `docs/status/task-sessions.md`
- A4 [artifact] T4 (i) an idle session ends within two loop passes of `abort_all()`: `docs/status/task-sessions.md`
- A5 [artifact] T4 TH2-R1 / D15: cross-server roots and phases one by one with an external scheduler: `docs/status/task-sessions.md`
- A6 [artifact] T5 ASan+UBSan and TSan 11 of 11; g++ 14 ASan+UBSan 11 of 11: `docs/status/task-sessions.md`
- A7 [artifact] K9 `live_frames()` back at baseline; docs and CHANGELOG current: `docs/status/task-sessions.md`
- A8 [artifact] A mutant per decision dies: `docs/status/task-sessions.md`
- A9 [artifact] 0 extra allocations per accepted connection against head: `docs/status/task-sessions.md`
- A10 [artifact] CI green on every job: `docs/status/task-sessions.md`

## Contracts

None recorded: landed work; its interfaces are in the code and the docs the journal's Result names.

## Decisions

- Lead rulings R-a (bounded shutdown) and R-b (each phase runs the earlier ones), 2026-11-02.
- CR6 and CR7 accepted as built; CR8 `~Reactor` asserts no claimed loop.
- Risk high: connection lifetime and shutdown on every server (the auditor's mapping).

## May land early

None.
