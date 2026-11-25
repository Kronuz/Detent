# detent-review-nits: review nits: delta-plan noise claim, history find_if, mirror cleanup errors

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status detent-review-nits`.

## Outcome

Small review findings left open: `docs/perf/delta-plan-scaling.md:98` claims "-20%, within noise" without probe 911a1f (delta-plan-fixed-cost C2); `history.cc:510` does an O(n^2) `find_if` (namespace-followups N2); cleanup errors replace the original error in `mirror.cc:220-224, 321-323` (N6). After this quest the claim cites its probe or is restated, the lookup is linear, and the original error survives cleanup. rpc-lane-codec N5 (consensus error code 7) waits on the Xapiand lead and is not in scope.

## Acceptance criteria

- A1 A failing mirror step whose cleanup also fails reports the original error: a test that fails without the fix (--fails-without): jobs.
- A2 The history lookup is linear and the suite passes: jobs.
- A3 [artifact] The delta-plan claim cites probe 911a1f or is restated: `docs/perf/delta-plan-scaling.md`.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (R8).
