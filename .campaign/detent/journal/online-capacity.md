# online-capacity: journal

State: ready to land
Quest: .campaign/detent/quests/online-capacity.md (the lead's; never edit it)

## Doing

Nothing.

## Next

1. Lead: land.

## Blocked on

Nothing. Q1 answered (a) by the owner: D141 (Detent:16afa4dd); `restart-capacity` filed;
the `SHARED_LIBRARY_DECISIONS.md:231` agreement withdrawn by the lead. Main merged.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [decision] D141 (PLAN.md, Detent:16afa4dd): online increases dropped; capacity raised at a coordinated restart (quest restart-capacity). Reasons: `docs/ONLINE_CAPACITY_ARCHITECTURE.md` "Why not online" |
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
