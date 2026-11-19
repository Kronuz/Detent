# server-composition-adoption: detentd and detent-relay adopt the server composition note (task::lifecycle)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status server-composition-adoption`.

## Outcome

detent-authority (formerly detentd), detent-relay and detent-agent are composed the way cpp-libraries' docs/server-composition.md states once the Xapiand lead marks it CURRENT at a named commit: --upstream-ca, one limits struct, raise_open_files(max_clients + reserve) at start, the readiness pipe and pid flock (already in detent_cli), lifecycle/component services and stop handling, no fixed-interval polling (loop-idle-waits). Start only after the note is CURRENT and Xapiand's adoption has landed, so the shapes match.

## Acceptance criteria

- A1 [artifact] Each main follows the note's steps in order, deviations listed with reasons in the note's Mains table: `docs/` mechanism note for Detent's mains.
- A2 detent-authority, detent-relay and detent-agent start, serve and stop through the shared lifecycle; a test stops each with SIGTERM under load and sees in-flight requests finish within the shutdown bound: jobs.
- A3 The suite and CI pass: jobs.
- A4 One config shape (D125): each daemon reads detent-<role>.json holding process and role settings, with secrets in one private file named by path; authority.json and relay.json and agent.json are gone, and a test starts each daemon from its single file plus secrets file and refuses a secrets file with group or other access: jobs.

## Notes

Filed 2026-11-19 (Xapiand lead's audit, msg-98). Waits on the note's CURRENT commit and the owner's lifecycle/component rename ruling.
Audit 2026-11-19 (J10): the cpp-libraries README links `docs/server-composition.md` (server-composition journal:14); a one-line cpp-libraries commit beside this quest.
- The note is CURRENT at cpp-libraries:0579a1a (2026-11-19, msg-102). Adopt against that commit.
- D125's secrets rule goes into cpp-libraries docs/server-composition.md's Options step for all five daemons, Xapiand included (agreed with the Xapiand lead, 2026-11-19): draft it as a follow-up commit on cpp-libraries, and the lead sends the diff to the Xapiand lead to confirm before it lands.
- task::lifecycle landed at task:4b6a096; its R1 (post_call starvation) and R2 (flush skipped when off_loop can't start) fixes landed at task:b7e2449 (msg-111, msg-114). Adopt at or after task:fb32493: both NDEBUG layout fixes landed (task-driver-layout task:63b43bd, task-asio-layout task:fb32493), and a whole-repo sweep found no other conditional member (msg-117, msg-118).
