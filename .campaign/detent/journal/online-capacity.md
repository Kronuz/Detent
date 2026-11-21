# online-capacity: journal

State: ready for review
Quest: .campaign/detent/quests/online-capacity.md (the lead's; never edit it)

## Doing

Nothing: the ruling is written; it waits on the lead's decision id and the owner's confirmation.

## Next

1. Lead: record the decision (proposed text below) in PLAN.md Decisions; ask the owner the
   question under "Blocked on"; file `restart-capacity` if he agrees.

## Blocked on

Owner (via lead), because it narrows a documented capability and withdraws a shared-library
agreement (both reserved decisions):

- Q1. Drop online capacity increases and replace them with a raise at a coordinated restart?
  Options: (a) drop online, build the restart raise (recommended); (b) build the online
  blueprint (redesign first: it predates namespace history); (c) drop both: a raise is then
  RC-4's new-epoch restore, with agent reset and new producer sessions.
  What users lose with (a): a raise without a publication pause. Reads never pause.
  Promised where: `docs/CAPACITY_RUNTIME.md:32`, `docs/CAPACITY_QUALIFICATION.md:26,30,70`,
  `docs/status/README.md:33`, startup-runtime-capacity A3 ("online profile activation"), and the
  October 23 agreement with the shared libraries, `docs/SHARED_LIBRARY_DECISIONS.md:231`
  (consensus Worker two-format admission). No website page promises it.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [decision] Proposed for PLAN.md Decisions (the lead assigns the id): "Online capacity increases are not planned: a group raises `keys`, `state_bytes` and `image_bytes` at a coordinated restart, each replicated entry applying under the limits it carries. Online growth costs a replicated command, a new image version and a shared consensus Worker change to avoid a pause of publication only; ceilings are bounds, not reservations. By: owner (scope), lead (quest)." Reasons: `docs/ONLINE_CAPACITY_ARCHITECTURE.md` "Why not online" |
| A2 | [artifact] `docs/ONLINE_CAPACITY_ARCHITECTURE.md`: Status "Not planned", the reasons, the restart-raise contract (rules 1-7), what it gives up |

## Result

`docs/ONLINE_CAPACITY_ARCHITECTURE.md` rewritten: online increases not planned; a raise at a
coordinated restart replaces them. Reasons, each checked against the tree at this branch:

1. Ceilings are bounds: `State` reserves nothing by keys/state/image (`state.cc:764-780`); journal
   pools are logical (`service.cc:370-380`; consensus `worker.h:279` is an admission check). The
   recorded 1K trial under a 5M profile: 89.6 MiB summed over seven services
   (`CAPACITY_QUALIFICATION.md`, October 23; not re-measured).
2. Every `DTABAT02` and `DTAADM01` entry already carries all ten limits (`state.cc:1207`, `:1813`),
   so replay under the carried profile is deterministic with no new format; only the equality
   checks change (`state.cc:1236`, `:1828`, `:2124`). Command digests (`DTACMD01`, `state.cc:299`)
   carry no limits.
3. The blueprint is stale: it names `DTAIMG01` (now `DTAIMG03`, `state.cc:1983`) and ignores
   namespace history, which decodes against limits and sizes the peer fetch lane from them
   (`history_format.hh:51-139`, `service.cc:528,654`).
4. RC-3 and RC-4 do not need it; reads survive authority restarts (`ACTORS.md:163`).
5. Agent images bind the delivery profile (`include/detent/cursor.hh:33`), not authority limits.

Build quest for the lead to file (if Q1 is (a)):

- `restart-capacity`: raise authority capacity at a coordinated restart. Risk: high (replay
  semantics and recovery checks; no new bytes). Files: `src/detent_authority/state.{hh,cc}`,
  `src/detent_authority/node.cc`, `src/detent_authority/service.cc`,
  `tests/detent_authority/test_state.cc`, `tests/detent_authority/test_node.cc`,
  `tests/detent_authority/test_service.cc`, a `tests/detent_system/` restart case,
  `docs/mechanisms/authority-capacity-raise.md`, `docs/CAPACITY_RUNTIME.md`,
  `docs/CAPACITY_QUALIFICATION.md`, `docs/status/README.md`,
  `website/src/content/docs/reference/configuration.md`. Contract: the doc's rules 1-7.
  - A1 A group that failed `Capacity` at the old key ceiling replays after the raise with the
    same result: jobs, `--fails-without` a mutant applying under configured limits.
  - A2 An image and an administrative entry recorded under the old profile recover under the
    raised configuration; a lowered field refuses and names it: jobs.
  - A3 A retry of a pre-raise producer sequence returns its original receipt: jobs.
  - A4 Three voters restarted with raised `keys` grow past the old ceiling; relays and agents
    keep their generations and serve the new keys: a system job.
  - A5 [artifact] Every `State::limits_` site classified replicated or local:
    `docs/mechanisms/authority-capacity-raise.md`.
  - A6 Suite and sanitizers: jobs.
- The `SHARED_LIBRARY_DECISIONS.md:231` agreement is withdrawn by the owner's ruling; the lead
  edits that record (outside this quest's file set).

## Rulings I made

1. Kept the old blueprint out of the doc and cited `Detent:03da5be5` for it: the text is stale
   against main and git keeps it.
2. Proposed the restart raise instead of a bare drop: the entries already carry their profile,
   so it costs no format, wire or library change, and it spares users RC-4's new-epoch restore.
3. No measurement job: the criteria are [decision] and [artifact]; the one figure cited is the
   recorded October 23 trial, named as such.
