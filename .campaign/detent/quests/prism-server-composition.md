# prism-server-composition: Prism adopts the server composition note (D122)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status prism-server-composition`.

## Outcome

D122: every Kronuz daemon ends up with one shape in the server-composition areas. Prism (src/main.cc) is composed the way cpp-libraries' docs/server-composition.md states once it is CURRENT: --upstream-ca where it dials upstream, one limits struct, raise_open_files at start, the readiness pipe and pid lock, lifecycle/component services and stop handling, housekeeping on the loop's wheel, no fixed-interval polling. It follows Xapiand's and Detent's adoptions so the three match.

## Acceptance criteria

- A1 [artifact] Prism's row in the note's Mains table matches its main step by step, deviations with reasons: `ARCHITECTURE.md`
- A2 Prism starts, serves and stops through the shared lifecycle; a test stops it with SIGTERM under load and in-flight requests finish within the shutdown bound: jobs.
- A3 The build and CI pass: jobs.

## Notes

Filed 2026-11-19 (D122). Prism has no tests today: A2's test is the first; add the CMake test wiring it needs.
