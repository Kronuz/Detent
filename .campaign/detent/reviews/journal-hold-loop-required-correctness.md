# journal-hold-loop-required review: correctness

Commit: journal:1222e3b33f12f6f97a168d37de03b2c0b17b45fd
Verdict: APPROVE
Date: 2026-11-20

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261119-230956-1c62ec: `wake_refused` (which runs `hold_loop`) passes at journal:1222e3b.
- 261119-231013-d62978 (test-only journal:32172e7) fails 3 checks: "does not compile", and "an
  empty loop is refused" for both holders. 261119-230959-49f27c (`hold-any-loop` mutant at
  journal:1222e3b) fails the two "refused" checks. Both logs read; the test fails without the change.
- 261119-231000-3ff92a (suite), 261119-231002-971376 (TSan), 261119-230837-f9b88b: exit 0 at journal:1222e3b.
- 261119-231003-aa3407: consensus adb918f built and its ctest passes with journal from this commit.
- CI run 44692769460: `success`, head_sha journal:1222e3b (checked through the GitHub API).
- 261119-231005-12b22a and control 261119-232342-d1e01c (journal merge base journal:bb114f1) both
  fail at Detent configure with the same `completion::task` target-not-found errors at
  src/CMakeLists.txt:63 and src/detent_authority/CMakeLists.txt:27: a pre-existing pin gap.

## What I checked myself

No probe jobs: reading settled each question.

1. Throw leaves the driver untouched. include/journal/turn_driver.h:374-375: the empty check is the
   first statement in the constructor body; only the `turns` reference is bound before it. It
   precedes the `loop_mutex_` write and the `holder_` write, and a throwing constructor runs no
   `~Hold`, so nothing is half-acquired or released. An empty loop on an already held driver throws
   "a Hold needs a loop" and leaves the other holder intact. `scheduler_ref`'s `operator bool` is
   `self_ != nullptr` (task:95798a0 task.hh:239), so "empty" means a default-constructed ref.
2. Call sites. journal: turn_driver.h:315 (`run()`, `task::current()`, already refused by the
   `posts_calls` check at :314 when empty, so the new check is unreachable there) and blocking.h:91
   (`scheduler_ref::of(scheduler)`, never empty). consensus origin/main driver.h:1031 passes
   `scheduler_ref::of(scheduler)`, as do the active consensus worktrees (batch-turn-room,
   blocking-hold-loop, peers-attempts-bound, heal-snapshot-stall, xapiand consensus-sim-loops). The
   three worktrees still using the two-argument form (connection-slow-reader-flake,
   consensus-dns-threads, transport-feed-scan) belong to landed quests, 0 commits ahead of main.
   Detent main and the Xapiand checkouts name no `TurnDriver::Hold`; Detent includes only
   `journal/mutation.h` directly (src/detent_authority/service.cc:25). The other `Hold`,
   `journal::Host::Hold` (host.h:353), is a distinct class with a private `(Host&, group)`
   constructor; the diff does not touch host.h and its behavior is unchanged.
3. Compile-time check. test/turn_seal_test.cc:567 tests
   `!std::is_constructible_v<Hold, TurnDriver&, Holder>`: `Hold` has a user-declared constructor, so
   it is not an aggregate and the trait matches direct and brace initialization. Evaluated as a
   runtime `check` rather than a `static_assert`, so the test-only commit still builds and fails
   (d62978's first FAIL line) instead of not compiling. The throw is tested at runtime for both
   holders at :568-574 with the exact message, then a valid Hold holds and releases (:576-580).
4. Docs. CHANGELOG.md (2026-11-19, Changed) and docs/mechanisms/turn-seal.md:226-227 and the
   `wake_refused` row match the code and are in the present tense; the CHANGELOG's past-tense clause
   describes the old behavior, as a changelog does. See F2 for one overreach.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Detent's half of A2 rests on reading, not a build: both the job and its control fail at configure on the pre-existing `completion::task` pin gap. By reading, Detent constructs no `Hold` and reaches one only through consensus driver.h:1031 and journal blocking.h:91, both passing a loop. | 261119-231005-12b22a, 261119-232342-d1e01c | None for this quest; the pin gap is its own repin. |
| F2 | no | nit | turn-seal.md:227 "so no holder is deaf to `wake()`" says more than the check does: `Hold` refuses only an empty loop, and a non-empty loop whose scheduler declares no `post_call` is still deaf. Every current holder is safe (`run()` checks `posts_calls`; the blocking entries use `blocking_scheduler`). | include/journal/turn_driver.h:375, :120 | Say "so no holder lacks a loop", or have `Hold` check `detail::posts_calls(loop)` (an owner's call: it posts a no-op on every hold). |
| F3 | no | nit | "the refused Hold leaves the driver free" (test/turn_seal_test.cc:573) passes under the `hold-any-loop` mutant too, since the accepted Hold is destroyed inside the `try`. It does catch a check moved after `holder_ = holder`, which is the half-acquired case. | 261119-230959-49f27c log: 2 failures, both "refused" | None needed. |
| F4 | no | nit | turn-seal.md:228 is now a 169-column line in a file otherwise wrapped near 100. | docs/mechanisms/turn-seal.md:228 | Rewrap the paragraph. |

## Not run

- No probe jobs; no Detent build with the pin gap worked around.
- Other platforms and sanitizers beyond the TSan job and CI.
