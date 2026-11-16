# drivers-task-wheel: async-drivers 1: task deadlines and asio timers on Kronuz/wheel, blocking_scheduler

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status drivers-task-wheel`.

## Outcome

Kronuz/task's deadlines and its asio scheduler's timers run on the one Kronuz/wheel (D72, superseding the design's private `task::timer_wheel`), with the teardown, cross-thread and blocking contracts of `docs/design/async-drivers.md` (Detent), Build plan row 1 (D4, D5, X3, X7, X14, Y1, Y7, R3). task already includes `wheel.hh` and has `post_call`/`arm`/`cancel` in `scheduler_ref`: the first step is a gap list of row 1 against task main, in the journal, and only the gaps are built.

## Acceptance criteria

- A1 [manual] The journal's gap list maps every clause of design row 1 to task main's code (present, with a test name) or to a commit of this quest: the lead checks the list
- A2 `deadline<S>` runs on `S::clock` and `inline_scheduler::advance` expires it with no real time; a deadline beyond a round never fires early; `expire` tolerates a fire cancelling or re-arming any entry, itself included, and matches a reference model under random arm, cancel and re-arm from fires: tests, plus a mutant that fires a re-armed entry early, killed
- A3 `asio_scheduler::start_timer` is on the wheel with task's deadline contract unchanged: created and destroyed on any thread (`asio_test.cc`'s existing cases pass unchanged, plus an off-loop case), no added allocation, a wheel destroyed or `close()`d with deadlines armed releases them, and `destroy_roots` of frames holding deadlines runs clean: the suite under ASan and under TSan (two jobs)
- A4 `task::blocking_scheduler` (D5, X3): thread-safe `post` and `post_call` wake its wait, and it never waits without an armed timer; the `blocking_entry threads` and `lost-wake-live-token` cases are tests: the suite under TSan
- A5 [artifact] `docs/cancellation.md` and D4's list of what moves onto the wheel and what stays are current, and the Xapiand lead reviewed the diff: `ARCHITECTURE.md`: "wheel" and the Xapiand lead's reply named in the journal

## Contracts

Provides task's scheduler surface for journal-driver and consensus-driver: `scheduler_ref` with owner-thread `arm`/`cancel` (the wheel private), `inline_scheduler` with same-thread `post_call` and `advance`, `task::blocking_scheduler`. Consumes Kronuz/wheel at the pin task already uses.

## Decisions

async-drivers split (D96, lead): this is quest 1 of the design's build plan; D71/D72 settle Q1 (the wheel is Kronuz/wheel). Xapiand lead asked for M1b first (D91 Xapiand cluster release).

## May land early

Tests and the gap list; the scheduler changes land with the quest.
