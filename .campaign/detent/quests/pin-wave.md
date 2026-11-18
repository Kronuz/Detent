# pin-wave: Kronuz pins to their heads after the handoff branches land

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status pin-wave`.

## Outcome

In one pin commit, after the handoff's branches land, every Kronuz pin in `cmake/dependencies.json`
moves to its library's head, journal's framing (`journal:5bbeedc` or later) included, with each
library's CI green first and the move agreed with the Xapiand lead. Snapshot JSON, wire bytes and
persisted images are unchanged, and the hot path is measured against an A/A control.

## Acceptance criteria

Sources: handoff Follow-ups 4 ("take them in one pin commit after the branches above land, CI
green"); lead ledger criterion 6; M1's done criteria in `Detent:7c007ce:docs/PLAN.md` applied to this
wave ("M1 n"), as the procedure of `cpp-libraries/docs/UPGRADING-2026-10.md` requires.

- A1 Every Kronuz pin equals its library's default-branch head at the move, outside every "Commits not to pin" range, and equals what each declaring library pins (M1 1): the `pins` gate
- A2 [manual] Each moved library's CI is green on every job at the pinned commit (handoff rule 8; M1 2): the lead lists the runs
- A3 [artifact] The Xapiand lead's agreement to the wave is recorded (lead plan rule 4): `docs/SHARED_LIBRARY_DECISIONS.md`
- A4 The full native suite, the Python and Rust binding checks, ASan/UBSan and TSan in separate builds pass (M1 4): the `native`, `asan` and `tsan` gates
- A5 The input classes of the moved libraries are diffed through the old and new builds; snapshot JSON, wire bytes and persisted images are identical, and every difference has a library commit as its cause (M1 5): a job running `tools/w5`
- A6 The hot path (`reader_bench`, the managed first read, daemon publish and get) is measured at old and new pins with `ab_compare.py` and an A/A control, under `strict` holds of at most ten minutes (M1 6): jobs
- A7 [artifact] The CHANGELOG names each library moved (old pin, new pin), the sections acted on, the results that changed and the owner decisions taken (M1 7): `CHANGELOG.md`
- A8 [artifact] Dependency docs current (M1 8): `docs/DEPENDENCIES.md`

## Contracts

Consumes every Kronuz library at its head; changes no Detent API (per the Xapiand lead,
2026-11-04). Moves `cmake/dependencies.json` for every quest after it.

## Decisions

1. Needs `reader-s6`, `legacy-h`, `namespace-s3b`, `namespace-s1-s2` and `dense-pages` landed
   (handoff: "after the branches above land").
2. Open (lead, with `xapiand-74`): Detent pins no strata; which libraries strata's cold-tier items
   5 and 5b and manifest-log steps 1 and 2 moved is to confirm with the Xapiand lead.
3. Open (lead): A2-A6 carry M1's criteria to a later wave, a judgment of this mapping; the lead
   confirms them before the quest starts.
4. threadpool's head no longer has the `ThreadPool` family (one pool, 2026-11-02): Detent "stays on its
   pinned threadpool and migrates itself". The `WorkPool` equivalents are the table "Note for the Detent
   lead" in threadpool `docs/status/unify-pool.md`; task's head replaces `lock_until`/`lock_for` with
   `lock(token)` and http's head is the task API (W2a). Source: audit, 2026-11-08.

## May land early

None.
