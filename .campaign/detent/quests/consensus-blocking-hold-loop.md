# consensus-blocking-hold-loop: consensus driver's blocking entry hands its scheduler to Hold, so a cross-thread wake ends its sleep

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-blocking-hold-loop`.

## Outcome

consensus `driver.h:1011` builds its journal `Hold` with no loop, so its blocking entry stays deaf to a cross-thread `wake()` (journal-blocking-wake review N1). It passes its scheduler, as journal's `blocking.h` does after journal-blocking-wake, and a wake from another thread ends the entry's sleep at once.

## Acceptance criteria

- A1 A cross-thread wake ends the driver's blocking sleep within 50 ms; the test fails without the fix (--fails-without): jobs.
- A2 TSan passes the driver's threaded cases, and the suite and CI pass: jobs.
- A3 [decision] Whether journal makes `Hold`'s loop required, so no caller can build a deaf one, is recorded in PLAN.md.

## Contracts

Consumes journal `Hold(…, scheduler_ref)` from journal-blocking-wake.

## Decisions

Open: make the loop required in journal (the lead, A3).

## May land early

The test alone.
