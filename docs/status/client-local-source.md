> Status: CURRENT (2026-11-04) Ledger for the client local source design workstream (bindings after legacy wave 2 step H).

# client-local-source

- Design: [docs/design/client-local-source.md](../design/client-local-source.md).
- Branch / worktree: `design/client-local-source` from `Detent:3e8bdbc`, at `~/code/Kronuz/Detent-wt/client-local-source`.
- State: designing (ready for preliminary review).
- Done criteria (set by the lead; contributors tick, never edit):
  - none set yet
- Done:
  - Draft D1-D6, T1-T6, Q1-Q2, B1-B6 (`Detent:38afcb5`).
- Next:
  1. Preliminary architect review.
  2. Owner answers Q1 (does H delete `detent::Client`) and Q2 (what replaces `health()`).
  3. B1-B5 may start once reader S6 (`reader/s6`) is on main; none waits on Q1 or Q2.
- Blocked on:
  - B6 (step H's C++ deletions) on Q1, owner.
- Design vs code: not applicable (nothing built).
- Last verified: citations read at `Detent:3e8bdbc` (main), `Detent:70f648f` (`reader/s6`) and `Detent:d18d6f5` (`legacy/wave2-f`) on 2026-11-04; nothing built, run or measured.
- Finding for the lead: no CI workflow builds either binding today (`.github/workflows/ci.yml` has no Python-binding, `cargo` or `pybind` step; `DETENT_BUILD_PYTHON` and `DETENT_BUILD_RUST` default `OFF`, `CMakeLists.txt:47-48`), so the current binding tests never run in CI. T6 adds the job.
