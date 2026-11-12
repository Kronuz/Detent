# logger-on-wheel: The logger's deferred lines on Kronuz/wheel

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status logger-on-wheel`.

## Outcome

logger.cc's deferred lines live on a `wheel::timer_wheel` the LOG thread owns, as arm D of logger-wheel-slim (`bench/wheel_ab/d/logger.cc` at logger:afe86c5, changes 2 and 3, the sorted batch, no heap); the logger's own deadline structure is gone (D72, D86).

## Acceptance criteria

- A1 The suite passes, plus a test that a deferred line fires once, in order with its peers, no earlier than its due time and within one tick plus the LOG thread's wake: jobs
- A2 TSan over the suite and the deferred-line stress test: a job
- A3 The production logger matches arm D's numbers: the wheel_ab rows p3 and p2 at P=1, 4, 14 with the production logger as candidate against arm D as reference, by D77's paired statistic, none past 1.10: exclusive jobs
- A4 A grep job finds no deadline heap or second timer structure left in logger.cc
- A5 [artifact] ARCHITECTURE.md says deferred lines ride the LOG thread's wheel, their 1 ms resolution and why: `ARCHITECTURE.md`

## Contracts

Consumes `wheel:timer_wheel` (wheel:bfd72bc).

## Decisions

D72, D78, D86.

## May land early

Nothing.
