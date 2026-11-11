# wheel-evidence: One timer wheel for Kronuz: stash or a new wheel, by measurement

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status wheel-evidence`.

## Outcome

A measured answer to which timer wheel the Kronuz libraries use, before task-wheel (async-drivers
quest 1) builds anything. async-drivers D4 compared its prototype wheel (`async-drivers-bench/
wheel.hh`) only with Kronuz/scheduler's `SchedulerQueue` as it is (a `shared_ptr` task per arm,
cancelled tasks left filed, a fixed `steady_clock`) and with one asio timer per deadline. It never
measured Kronuz/stash itself (a lock-free multi-level wheel, the structure under scheduler) or an
improved stash. One mechanism should serve every library (the owner's isometry rule); a second
wheel is acceptable only for a measured gap the owner accepts.

## Acceptance criteria

- A1 Candidates, each in `docs/design/async-drivers-bench/wheel_vs_stash*`: (a) the prototype
  wheel; (b) stash as it is, with raw-pointer values; (c) stash improved for a loop owner (an
  intrusive entry with O(1) unlink, an injectable clock, no allocation per arm), as small a change
  as makes it competitive; (d) any other shape the contributor finds that unifies both uses.
- A2 Measured as `exclusive` jobs at named commits, 1,000 and 1,000,000 entries: bytes per entry,
  heap per arm, arm, re-arm, cancel and expire on one loop thread; arm from 1, 4 and 14 other
  threads (stash's case: Kronuz/scheduler, debouncer, logger); and whether each runs on a manual
  clock for the simulator. Each number with an A/A spread.
- A3 [decision] The decision rule, written in the design note before the first measured run: the
  unified candidate wins if it is within 10% of the best on the single-loop path and within 10% of
  stash on the cross-thread path; otherwise the note gives both numbers and the lead brings the
  gap to the owner. The PLAN.md decision records the result, and task-wheel's quest follows it.
- A4 [artifact] `docs/design/wheel-evidence.md`: the candidates, the numbers, the rule and the
  pick, and what it means for Kronuz/scheduler and Kronuz/stash (improve, keep, or retire: the
  owner's call, with the evidence).

## Contracts

None changed; the result shapes task-wheel and may add a stash quest.

## Decisions

- Owner, 2026-11-11: no library is retired or deprecated without trials and benchmarks; async-drivers
  Q1 and Q6 wait for this quest.

## May land early

None.
