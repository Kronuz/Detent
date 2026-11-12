# logger-followups: logger: review follow-ups and the scheduler link

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status logger-followups`.

## Outcome

The non-blocking findings of `reviews/logger-on-wheel-correctness.md` are closed, and logger no longer links Kronuz/scheduler (D72: scheduler retires once nothing uses it).

## Acceptance criteria

- A1 F2: `test/deferred.cc` cannot abort when the producer is preempted near a line's due time (no `CHECK(h.clear())` on lines due within a few ms, or a margin that holds): the suite run 50 times under load as one job
- A2 F6: logger's CMake links no scheduler target and no file includes scheduler headers: a grep job and the suite
- A3 F1: the batch orders by one call-time key with the old tie-break restored: a test with equal due times
- A4 [artifact] F4: ARCHITECTURE.md describes the batch's inserts as they are: `ARCHITECTURE.md`

## Contracts

None.

## Decisions

D72, D86.

## May land early

Nothing.
