# retirement-close-race review: correctness

Commit: 5772d201eaa0bffe55f583da2bd4847cb9978087
Verdict: APPROVE
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

## Evidence I relied on

All job ids `261109-`. The contributor's jobs ran at Detent:8c21fad, which differs from
Detent:5772d20 only in the journal (`git diff --stat 5772d20 8c21fad`), so they hold for the
reviewed code.

- A1: 091902-d4c641, the probe against base 018202d's retirement.cc (blob dd31703, main's): lost
  wakeup with 0 residents at round 77,150 (-O0) and 47,625 (-O2). Real stalls, not build failures.
- A2 positive: 091903-cdd46b passed. Revert: 091904-2f2be3, head's test against 10b4b13's
  retirement.cc (the fix's parent, seams present, old two-load logic) fails with "lost wakeup, drain
  pending with 0 residents" for close and rollback at `worker_admitted`. That is the defect, not
  "interleaving not reached" and not exit 125.
- Mutants, each exit 0 with the diff applied in the log and the failure message naming a lost
  wakeup (not a build failure, timeout or unreached interleaving): reread-admission 091905-c0b74a
  (close at `worker_admitted`), swap-rollbacks-admission 091906-df6aa6 (rollback at
  `worker_admission`), drop-closing 091907-3f61f4 (close at `worker_admitted`).
- A3: 091909-9f78a1, 300,000 rounds drained at -O0 (34 s) and -O2 (27 s).
- A4: tsan gate 091524-3d7822 at 5772d20, exit 0; `test_distribution_retirement` (which holds
  `drains_across`), `_factory` and `test_local_publication_retirement` pass under TSan (Debug, -O1).
- A5: native gate 091524-b72f0a at 5772d20, exit 0; 221 tests, 1 skipped
  (`test_local_trusted_paths`, a skip, not this quest's).

## What I checked myself

Scripts are in `.scratch/review-retirement-close-race-correctness/` (`probe.sh`, `stress.cc`). All
jobs ran at 5772d20.

1. Lost-wakeup invariant, by reading `src/detent_distribution/retirement.cc` at 5772d20. Every
   producer writes its marker, then the wake, then the final handoff: close at :376-379, rollback
   at :281-285, release at :258-262. The worker reads the pipe in `tick` (:56-61) before every
   `step`, except after a pass that did work, and that pass is followed by another `step` anyway.
   In `step` it loads `rolling_back`, then `closing`, then `admission` once (:143-146), and decides
   both drain and pending from those values (:148-151).
   Invariant: if a pass neither drains nor reports pending, every handoff it missed still has its
   wake byte unread. The pipe write and read order the producer before the worker.
   - Close: if the read consumed close's byte, `closing.store(release)` happens before the
     `closing` load, so `closing_seen` is true. Then either `admitted` has the bit or the
     `closing_seen && !(admitted & closed)` term makes the pass pending. A `closing` load of false
     means the byte had not been read, so the next `poll` returns.
   - Rollback: if the byte was consumed, `fetch_add` happens before the `rolling_back` load. A
     nonzero value makes the pass pending. A zero must come from this rollback's
     `fetch_sub(release)` or a later RMW, since coherence puts each thread's -1 after its own +1.
     Either joins the release sequence, so the acquire load synchronizes with it, and the
     `admission.fetch_sub` before it is visible to the later `admission` load.
   - Release: the node is in `waiting` until `ready_` (pending). A node exchanged into `incoming`
     after the worker's exchange has its wake after that exchange, so its wake is unread.
   - Close racing an admission: `admit_resident`'s CAS between `closing` and `fetch_or` leaves a
     resident whose release or rollback brings its own wake. Shutdown and the `domain()` catch path
     are unchanged: shutdown hands off before it signals, and the catch path's `reserved` slot is
     read as pending under the registry lock.
   - Spurious `poll` returns only add a pass.
   - The closing-after-admission mutant is equivalent, as ruling 3 says: `closing` only rises, and
     a consumed wake makes it visible.
2. Use after destruction. In `close`, after `fetch_or` nothing touches `runtime`. The new
   `at(close_handoff)` comes before the bit and reads only `state_`, which the domain owns
   (:378-382). In `undo_resident`, `at` comes before the decrement, and after the decrement only
   `state.rolling_back` is touched, which `make`'s local `state` copy keeps alive (`retirement.hh`
   :116). On the worker side, `step` and the drain path touch the domain through `pass[i]`, and the
   runtime is the worker's own: the runtime destructor joins it. In the test, `plan` outlives the
   runtime whose destructor joins the worker, so the hook never runs on a dead `Interleave`.
3. Determinism of `drains_across`, from the test file :67-145. The producer waits in its hook, so
   only `parked` lets the handoff land. The worker parks only on the second pass at its point after
   `woken`. On a one-domain runtime with no nodes, the pass before that one does no work, so a
   `tick` runs between them and the parked pass begins after the wake was consumed. No sleeps
   decide the order; the 5 s deadlines only detect a hang.
   - Probe 093507-2839ab: 100 of 100 runs pass at the fix with 4 yield-spinning threads.
   - Probe 093508-688edc: 10 of 10 runs against 10b4b13 fail, each with exactly the close and
     rollback lost wakeups at `worker_admitted`.
4. Concurrency stress against contention, which the contributor's probe lacks: it has one thread
   and no rollbacks. `stress.cc` runs close against 2 producer threads that make, fail
   constructors one time in three (rollbacks) and release, with a varying close delay.
   - Probe 093509-8252dd: at the fix, 20,000 rounds drain at -O0 (33,620 rollbacks) and at -O2
     (43,264 rollbacks).
   - Probe 093510-5e418c: at 10b4b13, it stalls with 0 residents at round 18,958 (-O0) and 10,526
     (-O2).
5. ASan+UBSan (separate build, `-fno-sanitize-recover=undefined`). Probe 093508-3c7505 runs
   `test_retirement` and 5,000 stress rounds at the fix, and both are clean.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no | gap | `campaign status` counts 0 of 5 criteria with evidence. The journal's Evidence table abbreviates job ids (no `261109-` prefix) and names the gates as "tsan gate 091524-3d7822", which the tool reports as "names no job". The jobs themselves are sound (above). | `campaign status retirement-close-race`; journal Evidence table | Write full job ids (`261109-091902-d4c641`, ...) in each row, A4/A5 included, so the land check reads them. |
| C2 | no | nit | A drain can now complete between a rollback's `admission -= 1` and its `rolling_back -= 1`. `rolling_back` is loaded before `admission`, so a rollback that starts after that load is not counted; main's order never allowed this. It is safe: only `state` is touched after the handoff, and `make` holds it. The mechanism doc does not say so. | `retirement.cc:143-148`, `:284-285`; `retirement.hh:116` | Optional: one sentence in `docs/mechanisms/retirement-wakeup.md` saying drainage may precede the final `rolling_back` decrement. |
| C3 | no | nit | The seam is not compiled out. Each domain per worker pass does two acquire loads of `interleave_hook` and a predictable null branch, as does each close and rollback, and each `RetirementState` grows by 16 bytes. On x86_64 an acquire load is a plain `mov`, so I judge the cost negligible but did not measure it. It does not change behavior with no hook installed. | `retirement.cc:81-82`, `:102-105` | None needed. Measure only if the worker pass shows up in a profile. |
| C4 | no | nit | The happens-before edge from `signal()` to `tick` is the pipe write/read, outside the C++ memory model. It relies on the kernel's ordering, which TSan models through fd sync. This assumption predates the quest; the mechanism doc could name it. | `retirement.cc:50-61` | Optional doc sentence. |

## Not run

- No platform other than macOS x86_64 with Apple clang. Linux, g++ and libstdc++ are CI's.
- No measurement of the seam's cost (C3).
- No mutant of `close`'s own order (marker, wake, bit): it predates the quest and is unchanged
  except for the seam call.
- The `swap` mutant's exact position relative to `closing` was not varied.
- I did not rerun the contributor's jobs: their logs at a code-identical commit were enough.
