# rename-detentd: Rename detentd to detent-authority (D123)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rename-detentd`.

## Outcome

D123: the authority daemon `detentd` becomes `detent-authority`, so the three daemons are named `detent-<role>` (detent-authority, detent-relay, detent-agent), and the binary matches its code (`detent_authority`). Greenfield: no alias, symlink or compatibility shim. The main moves where the other daemons' mains live (src/detent_relay/main.cc, src/detent_agent/main.cc pattern), so `src/detentd/` disappears.

## Acceptance criteria

- A1 `git grep -n detentd` on the landed tree finds only dated records (.campaign/, docs/measurements/, docs/reviews/, docs/status/ HISTORY files), each listed in the journal with why it stays: jobs (a check script that fails on any other hit).
- A2 The binary, CMake target, pid and log file names, help text, scripts, CI workflows, docs, guides and website say detent-authority; the system tests start the daemon under the new name: jobs (the suite).
- A3 [artifact] Every diagram that names detentd is updated at its D2 source and re-rendered in the house style (the d2-diagrams skill), with renders checked for overlap and size: `diagrams/01_overview.svg`
- A4 The suite and CI pass at the landed code: jobs.

## Notes

Filed 2026-11-19 (D123). The Xapiand lead updates cpp-libraries docs/server-composition.md and Xapiand's own mentions; tell them the landing commit. server-composition-adoption and prism-server-composition refer to detent-authority after this lands.
