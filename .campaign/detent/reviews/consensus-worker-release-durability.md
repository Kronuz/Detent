# consensus-worker-release review: durability

Commit: consensus:18f55c69946e
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This is the third round. The review at consensus:5de6c78 was CHANGES on F1: the failed close's
doubt was held by the Driver, not the Host. consensus:08cdf68 fixes F1 and N3 (below), and
consensus:18f55c6 merges main. Since 08cdf68 the merge changes only test/consensus_test.cc
(`git diff --stat 08cdf68 18f55c6`).

## Evidence I relied on

Round 3 jobs ran at consensus:08cdf68. I read the logs of the jobs I cite.

- 261119-191119-509297: second_driver (A7) passed. 261119-191121-9e2d59 runs it on 5de6c78's
  worker.h, driver.h and journal pin. It fails as F1 predicted: no refusal, "the turn log lost this
  group", and acknowledged `a`, `x` recovered as nothing. My probes agree. 261119-184141-7487c2
  fails at the old pin, and 261119-184142-2f5a77 passes once journal is repinned.
- 261119-191103-5a8902: failed_close passed.
- 261119-191116-d053b8 and 261119-191124-9c1a6d: these mutants drop the EIO-to-`close_in_doubt`
  naming. They fail only on the error's type. The id is still refused with the Host's EIO, so the
  naming is a label: durability does not rest on it.
- 261119-191140-4fdf57: N3's mutant fails entry_throw ("the loop's throw reaches the blocking
  entry's caller").
- 261119-191135-0d8700: close_crash at 200 seeds passed. 261119-191137-b98c7e: its loop-throw mutant
  still fails.
- 261119-191150-03bd0f: ASan+UBSan passed. 261119-191152-8abf98: the suite passed.

## What I checked myself

I read `git diff 5de6c78..18f55c6` and journal at the new pin (journal:9e11713, which contains
8c92ed9). These parts matter:

- `Host::hold`
- `holding`
- `resident`
- `update`'s rollback
- `held`
- `Journal::create_shared` and `recover_shared`

A copy of 9e11713's host.h is in `.scratch/detent/review-consensus-worker-release-durability/`.
The `second_driver.py` and `second_driver.sh` probes in that directory are what became A7.

**The question: can "the Store holds nothing" let an in-doubt group be recovered?** No. The
condition `!host_.holding(id)` (driver.h `opening_error`) only picks which error the caller sees.
It runs in the catch, after the refusal has already happened. The refusal is the Host's, and both
Store paths reach it first:

1. `create_shared` and `recover_shared` call `host_->hold(group_)` before any table or log
   operation.
2. `hold` checks `resident(group)->in_doubt` before anything else and throws EIO.
3. `held()` also refuses an in-doubt entry, so `close_group` and `reopen_group` cannot act on it.

The mark is not dropped while the Host lives:

- `update` erases only closed entries, and a failed commit restores the entry to open with
  `in_doubt` set.
- Only a Host open reloads the shards.

Both misclassifications refuse the id; they mislabel, they do not leak:

1. **Mark set and the id held.** This happens when the failing Store has not ended yet. The caller
   gets the raw EIO, and the id is still refused.
2. **Mark clear, nothing held, and an EIO.** This needs an EIO inside `hold` before the insert, and
   `settle` does no disk IO. After a hold succeeds, `hold_` lives in the Worker's `store_` member,
   so a later disk EIO reports `holding` true and keeps its own error.

d053b8 and 9c1a6d show the first point: with the naming removed, the id is still refused.

Other checks:

- `in_doubt_` and `close_failed()` are gone, so there is one mechanism, in journal.
- `consensus::run` rethrows `loop_result.error` before `rearm()`, so the Driver stays stopped after
  a loop throw (driver.h, the end of `run`).
- DRIVER.md now says a throw leaves the outcome unknown (old N2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | defect | Fixed. The doubt is the Host's (journal:9e11713 `Host::hold`), and every Driver over that Host gets `close_in_doubt`. | 509297; fails without: 9e2d59, 9c1a6d | None |
| N1 | no | gap | No test drives a disk EIO through `create` or `recover` after the hold to show it keeps its own error. By reading it does (`hold_` outlives the throw). It is a label, not durability. | journal.h `recover_shared`, driver.h `opening_error` | Optional: a FlakyIO read EIO during recover, expecting `std::system_error`, not `close_in_doubt`. |
| N2 | no | nit | Unchanged from the last round: failed_close injects only the first directory sync failure (driver_close_test.cc `fail_sync = 1`). | driver_close_test.cc | Optional: loop over 1..3. |
| N3 | no | defect | Fixed. `consensus::run` rethrows the loop's error and leaves the Driver stopped. | entry_throw; fails without: 4fdf57 | None |
| N4 | no | gap | Pre-existing and unchanged: `step()` fences only on `std::exception`. A non-standard throw leaves its group half-stepped. The Driver now stays stopped under the blocking entry. A reactor `run()` started again would still step that group. | driver.h `step` | Optional: fence each group in `loop()`'s `catch (...)`. |
| N5 | no | nit | Unchanged: the life after a throw (in-flight IO completions capture `this` and `&g`) is not documented for `~Driver`. | driver.h `launch`, `watch` | Optional doc line. |
| N6 | no | gap | `campaign status` lists A4 as met by 261119-191150-03bd0f, which is ASan+UBSan. A4 is TSan plus CI. The TSan job 261119-191147-b78d00 failed: the lead reports a quiet hold-name collision, and I did not read its log. Its rerun, 261119-203855-8c3501 at 18f55c6, was queued. | `campaign status` | Cite 8c3501 and CI for A4 before landing. |

## Not run

- TSan at 18f55c6 (8c3501 was queued) and CI at 18f55c6, which had no run listed when I wrote this.
  They gate the landing, not this review.
- A disk EIO during create or recover after the hold (N1).
- Owned (non-shared) Stores. Multi-voter rows.
