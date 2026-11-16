# journal-driver: async-drivers 3: journal TurnDriver and Store on task, one directory per host (D97 Q3)

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-driver`.

## Outcome

journal runs on task: `TurnDriver::run` and `Store` are tasks over completion's seam, and `journal::run` drives them to completion with no `io_context` (D2, D5, D8), per `docs/design/async-drivers.md` (Detent) Build plan row 3. One directory and owner lock per host holds every group (Q3 (b), D97): artifacts named by opaque fixed-width group id and incarnation, turn logs named by (generation, loop), a per-host group table, and a re-partition when L changes that keeps lost groups lost.

## Acceptance criteria

- A1 `TurnDriver::run` and `Store` are tasks; the crash-point suites run through `HeldCompletion` with the sync driver's histories, unchanged in outcome: the suite
- A2 `journal::run` with no `io_context` returns only after staged records are written (an append cancelled after staging included) and throws `journal::stalled` with the waiting groups' ids on every lost wake, a live token included, over `blocking_scheduler`: tests (the `blocking_entry rules` and `lost-wake-live-token` cases) under TSan
- A3 Layout (Q3 (b)): zero descriptors per idle group (an idle-cost job); artifacts named by opaque group id and incarnation so nested indexes never collide and reclaiming one never touches another (test); reclamation and admission by name with no directory scan; a per-host group table reopens a closed group without scanning the turn log; one owner lock per host, the same lock strata's cold tier takes (cold-tier Q1): tests and the idle-cost job
- A4 Re-partition when L changes is crash-tested at every durable step over a damaged old generation; recovery's lost ids stay lost after commit and after a crash at every step, and a lost id stays lost across retention (async-drivers note): `placement_test remap`, `repartition_lost` and a retention test
- A5 No Asio in `include/` and the on-disk layout is documented: a grep job and [artifact] `docs/`: "one directory per host"

## Contracts

Provides `journal::TurnDriver`, `journal::Store`, `journal::run` on task and completion's seam, and the per-host on-disk layout (a format change Xapiand 7b writes on; nothing reads the old layout, D43).

## Decisions

D96, D97 (owner: Q2 Sink, Q3 one directory per host, Q4 quiescence quest, Q7 node identity). Xapiand cluster-release (Xapiand:15c3e05ac) orders after this quest by this id.

## May land early

Tests of the existing state machines; the layout lands whole.
