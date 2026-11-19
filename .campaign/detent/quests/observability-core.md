# observability-core: knob observability OB1-OB3 in the reader core

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status observability-core`.

## Outcome

Knob observability is a goal (D37, D47, D48, D61) with a build plan OB1-OB6 (`docs/design/observability.md:312-321`), and D48/D61 defer OB1's gate to a core observability quest that does not exist: `git grep 'detent_usage\|ever_sampled' -- src include` is empty. After this quest the reader core has OB1 (the usage table), OB2 (ever-read bits) and OB3 (`detent_usage` in the C ABI); OB4-OB6 (hook, usage page and Prometheus textfile through the metrics library, bindings, fleet aggregation) stay for later quests.

## Acceptance criteria

- A1 `detent_usage` reports each knob's ever-read bit and usage count: tests through the C ABI: jobs.
- A2 Reader lookup cost with the bits on, before and after at named commits (strict, docs/perf/reader-lookup.md's bench); the D48/D61 gate holds: jobs.
- A3 The suite, TSan and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U2).
