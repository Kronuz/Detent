# consensus-late-append review: correctness

Commit: consensus:771e1bb820ca (branch quest/detent/consensus-late-append; code as at consensus:615e760, plus the ARCHITECTURE.md edit)
Verdict: APPROVE
Date: 2026-11-15

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- A1: 261115-213819-1f8a35 (peers `late-append` passes, slowest 236 ms) and its fails-without
  261115-213822-6700e2 (core.h from consensus:a95847e: commits FAILED, CHECK abort). Both at
  consensus:615e760; 771e1bb changes only ARCHITECTURE.md.
- A2: 261115-214258-d68788 (consensus_test with `late_append_responses`, `stale_append_responses`)
  and mutant 261115-213824-79ec49 (`response.term > hard_.term`, killed by the Figure-8 check).
- A3: suite 261115-213811-8d0872 (ctest, Release, transport on). TSan is CI's `linux-tsan`,
  running at 771e1bb when I checked; not judged here.

## What I checked myself

Scripts: `.scratch/review-consensus-late-append-correctness/mutant.sh NAME [TARGET ARGS]` (exits 0
iff killed) with `mutate.py` naming each mutant. Fresh checkouts at 771e1bb.

| Mutant | consensus_test | peers `late-append` |
|---|---|---|
| no-bound (drop `matched > last_index()`) | killed 261115-214741-539cd2 | |
| pop-strict (`through < matched`) | killed 261115-214751-65a374 | survived 261115-214803-e8156c |
| no-snapshot-guard (drop `peer.snapshot`) | survived 261115-214743-ff9772 | |
| late-failure-counts (drop `!response.success`) | survived 261115-214745-88078e | |
| verified-always | survived 261115-214748-86a335 | survived 261115-214805-8ef020 |
| no-next-raise (drop `next = max(next, matched+1)`) | survived 261115-214754-b13fd0 | survived 261115-214801-9e8223 |
| no-late-read-ack (late path skips `acknowledge_read`) | survived 261115-214756-6ae688 | survived 261115-214758-1006f8 |

Question 1 (safety), by reading `include/consensus/core.h` at 771e1bb:

- A success carries the follower's term, and a follower answers success only to a request whose
  term it adopted (core.h:858-905), so `response.term == hard_.term` (core.h:929) means this
  leader's term-T request. A leader of T never truncates its own log in T, and a follower in T
  only replaces entries that conflict with T's leader, so `matched` stays held. Becoming leader
  again needs T+1; `follower()`/`fence()` clear `peers_` (core.h:414, 420); old-term replies drop.
- Snapshot install: during one, the guard drops late successes. After one, `retains_suffix`
  (core.h:399) keeps the follower's suffix whenever its log matches at the boundary, which holds
  whenever the follower held T's log through `matched >= boundary`. With `matched < boundary` the
  late success is at or below `peer.matched` and drops. Safe with or without the guard.
- Removed and re-added peer: a re-add is a new incarnation (ARCHITECTURE.md:29 "never reused"),
  and a removed peer's late reply finds no `peers_` entry (core.h:931).
- Leader compaction: `matched <= last_index()` is unaffected by the base; a late `matched` below
  the base leads to an install, as before.
- Monotonic: `matched <= peer.matched` returns early (core.h:945) and `acknowledge` takes the max
  (core.h:982). Commit still requires a current-term index on distinct voters; `matched` values
  are all true, so no entry commits without a majority.

Question 2 (retiring): flights are contiguous and ordered (`replicate`, core.h:1140-1142). Popping
`through <= matched` retires only flights whose entries the follower holds. `window` drops by
each popped flight's bytes. `next` stays within `last_index() + 1` given the bound. A surviving
front flight that overlaps `matched` re-sends held entries, and the follower skips them. For an
in-flight answer this is the old "this and every older flight". Timeouts still reset from the
front flight. I found no stall, leak or flight that is never retired.

Question 3 (amendment): consensus:615e760 changes only the success half of
`stale_rpc_and_failure_transitions`. It now expects a Persist, commit 2 and progress 2. The
failure half still requires empty output, and the higher-term half is unchanged. Nothing broader.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | R4 (a late success acknowledges its read probe) has no test: removing it survives both suites. Reads stay safe either way (probe and term checked, core.h:989-991), but the read-livelock claim is unproven. | 261115-214756-6ae688, 261115-214758-1006f8 | Add a consensus_test case: a read whose probe flight times out and is re-sent, and only the superseded answer arrives. `ReadReady` must follow. |
| F2 | no | gap | The `peer.snapshot` guard in the late path is untested. Reading shows it is not needed for safety (Q1), so it is hygiene. | 261115-214743-ff9772; core.h:943 | Test a late success during `AwaitSource`/`Sending`, or drop the guard with that reasoning. Owner's call. |
| F3 | no | nit | `verified` gating and the `next` raise in `acknowledge` are untested. Both affect pipeline efficiency only (duplicate re-sends), not safety. | 261115-214748-86a335, 261115-214754-b13fd0, 261115-214801-9e8223, 261115-214805-8ef020 | Optional: check `next` after a late success that passes every flight. |
| F4 | no | nit | Removing `!response.success` changes only whether a late in-term failure acknowledges a read. A failure in term T after the read's admission is a valid ReadIndex contact, so this mutant is nearly equivalent. | 261115-214745-88078e | None needed. |

## Not run

- TSan (`linux-tsan` CI at 771e1bb, running) and the rest of CI.
- No repeated runs of the timing-dependent `late-append` scenario beyond the five runs in the jobs above (all passed at 214-263 ms).
- Membership-change and install interleavings were checked by reading, not by new tests.
