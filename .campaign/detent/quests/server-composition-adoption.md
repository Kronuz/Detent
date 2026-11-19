# server-composition-adoption: detentd and detent-relay adopt the server composition note (task::lifecycle)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status server-composition-adoption`.

## Outcome

detent-authority (formerly detentd), detent-relay and detent-agent are composed the way cpp-libraries' docs/server-composition.md states once the Xapiand lead marks it CURRENT at a named commit: --upstream-ca, one limits struct, raise_open_files(max_clients + reserve) at start, the readiness pipe and pid flock (already in detent_cli), lifecycle/component services and stop handling, no fixed-interval polling (loop-idle-waits). Start only after the note is CURRENT and Xapiand's adoption has landed, so the shapes match.

## Acceptance criteria

- A1 [artifact] Each main follows the note's steps in order, deviations listed with reasons in the note's Mains table: `docs/` mechanism note for Detent's mains.
- A2 detentd and detent-relay start, serve and stop through the shared lifecycle; a test stops each with SIGTERM under load and sees in-flight requests finish within the shutdown bound: jobs.
- A3 The suite and CI pass: jobs.

## Notes

Filed 2026-11-19 (Xapiand lead's audit, msg-98). Waits on the note's CURRENT commit and the owner's lifecycle/component rename ruling.
Audit 2026-11-19 (J10): the cpp-libraries README links `docs/server-composition.md` (server-composition journal:14); a one-line cpp-libraries commit beside this quest.
