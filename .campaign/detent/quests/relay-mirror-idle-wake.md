# relay-mirror-idle-wake: detent-relay's mirror waits for upstream change instead of polling

Risk: high (relay to authority protocol use, idle cost). The lead writes this quest; contributors
read it and never edit it. Its machine state is in `state.json`: `campaign status
relay-mirror-idle-wake`.

## Outcome

Found by warm-relay-agent-boot (probe 261123-151055-68ee44): an idle mirror relay at the system
harness's `interval_ms: 10` (`tests/detent_system/managed.py:111`) uses 20.5 s of CPU in 120 s,
about 1.7 ms per sync that finds nothing new. At the default 250 ms (`configuration.cc:82`) that is
an estimated 0.7% of a core per idle relay, plus 4 authority requests a second; every relay in a
fleet pays it. `mirror_service.cc:405-409` re-syncs on a timer. loop-idle-waits removed the same
shape from the host turn and the authority service.

After this quest an idle mirror relay waits on its upstream (a parked read the authority answers on
change or deadline, or the authority's existing change stream) and syncs only when something
changed or a deadline passes, with no sync-latency regression.

## Acceptance criteria

- A1 Idle CPU of a mirror relay at the default configuration, main against the fix, strict jobs at
  named commits with an A/A band (`bench/idle-wakeups/relay_sample.py` or its successor): the fix
  is within noise of zero sync work at rest and wakes only on change or deadline; numbers in the
  Result, and what that is per relay in core-seconds a day.
- A2 Change-to-relay latency p50/p99 from an authority publish to the relay serving it, main
  against the fix, strict: no change beyond the band.
- A3 Tests: a relay with no upstream change makes no sync requests between deadlines (counted, not
  timed); a change wakes it; an upstream restart or failure reconnects with the existing backoff.
  A `--fails-without` job at main for the first.
- A4 [artifact] the relay mirror mechanism doc states how the mirror waits and what wakes it.
- A5 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

The relay's use of the authority's read protocol; no format change. A new authority endpoint or
parameter, if needed, is named in the design step and goes to the lead first.

## Decisions

Lead, 2026-11-23: its own quest, an efficiency defect outside warm-relay-agent-boot's scope.

## May land early

Tests that count sync requests, behind no behavior change.

## Tasks

None.
