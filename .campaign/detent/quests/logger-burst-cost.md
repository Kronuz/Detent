# logger-burst-cost: Logger: producer burst cost back to the f61d8df level on workpool and wheel

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status logger-burst-cost`.

## Outcome

A producer thread's cost per `L_INFO` call is back at the old pin's level while the logger keeps workpool and the wheel-backed deferred lines (D86). deps-workpool-repin's exclusive A/B (job 261115-214410-6f1bd9, `bench/lg.cc` via Detent's `benchmarks/logger_ab.py` on the repin branch, Detent:8d7561a3) measured logger:907d27f against logger:f61d8df: burst 1.48x at 1 producer (64.6 to 97.5 ns) and 1.75x at 4 (76.8 to 135.9 ns); back to back 1.06x and 1.12x; end to end 0.96x and 1.01x. The A/A band (job 261115-214405-b29364) is within about +/-8%.

## Acceptance criteria

- A1 The cause is named: a bisect over f61d8df..907d27f (suspects a07f3fe and the arm D series) with each step's burst rows, as exclusive jobs, and the mechanism explained in the journal
- A2 At the fix, against logger:f61d8df with the same `bench/lg.cc` and the paired statistic (D77), every burst and back-to-back row's median ratio is at or below 1.10x and end to end does not regress: an exclusive A/B job and its A/A job
- A3 The deferred-line behavior of D86 is unchanged: logger's suite, the deferred mutants and transcript golden pass: the suite and ci
- A4 [artifact] `CHANGELOG.md`: "burst"

## Contracts

No API change. deps-workpool-repin repins to the fix commit; Xapiand uses the same logger.

## Decisions

Lead, 2026-11-16: hold deps-workpool-repin until this lands rather than accept the hand-off cost (owner may overrule). If the per-thread queue is the cause and cannot meet 1.10x, bring the numbers and options to the lead before trading it away.

## May land early

Nothing.
