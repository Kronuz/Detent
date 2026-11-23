# driver-witness-removal-sink review: correctness

Commit: consensus:5f1223ccd5870341b9ddb620eff1c1bccf34a403
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The rule is sound for every live incarnation. A range is acknowledged only when this node is a
witness at a committed index (the range's last, or the apply index `first - 1`), and a witness
incarnation can never become a data member again. So the sticky flag never acknowledges a range
that an application that holds data needed. One path still hands the Sink a range after a hollow
checkpoint: a removed witness that checkpoints at or past its removal and then restarts (W1).
DRIVER.md states that path. It reaches only a dead incarnation, so it is not blocking. Whether to
close it is the owner's call.

## Evidence I relied on

- 261119-091207-dec2aa (exit 0): witness_promotion, witness_removal, witness_removal_restart and
  all of driver_test pass at 5f1223c.
- 261119-091209-95db09 and 261119-091211-f71e84 (fails-without, exit 1 as required): each job
  restores main's driver.h (4b0b214), and a grep guard exits 0 if the fix is still present, so
  neither can pass vacuously. The logs show the bug's symptom:
  - "Sink [1..2] [7..8] [9..9]; each range the Sink received starts after the last".
  - "Sink [7..7]; the Sink received through 0".

## What I checked myself

The probe scripts are in `.scratch/detent/review-driver-witness-removal-sink-correctness/` in
the home checkout: `rv_probe.sh`, `rv_cases.cc` and `rv_patch.py`. To rerun them, use
`sh rv_probe.sh <that dir>` in a consensus checkout.

- 261119-092040-c7d4af (queued when I wrote this). The script patches four cases into
  driver_test at 5f1223c:
  - `rv_both_in_one` must pass. It promotes at 3 and removes at 4 inside one range [3..4]. The
    expected result is that the Sink receives [1..6], contiguous, and the apply index reaches 6.
  - `rv_restart_flag` must pass. The witness checkpoints at 6 and restarts. Range [7..8], which
    contains the removal, is then acknowledged by the apply-index rule, and range [9..9] is
    acknowledged by the flag that [7..8] set. The Sink receives nothing.
  - `rv_post_removal_at` and `rv_post_removal_past` only report. They checkpoint at the removal
    (7) or past it (8), restart, and print what the Sink received. That is W1.

  If either of the first two cases fails, my answers to questions 2 and 3 are wrong and the
  verdict needs revisiting.

Answers to the lead's questions, from the code at 5f1223c:

1. **The ruling is consistent with D9, and no data member applies past an entry it never
   received.** A witness's apply index has passed its application's position since its promotion
   (DRIVER.md, "The one exception"). Acknowledging the removal's ranges extends the same named
   exception and creates no new gap. The same incarnation cannot come back as a data member, for
   three reasons:
   - Incarnations are never reused (`membership.h:128`).
   - A witness leaves only by removal (`core.h:674-678`).
   - Witness to voter counts as two changes (`membership.h:134`).

   A re-added node is a new incarnation, which means a new Driver Group. `open` throws while the
   group is open (`driver.h:182`), and close erases the Group (`driver.h:634`), so the flag dies
   with it. I read "refused by name" the way the contributor did (W2).
2. **The flag is never wrong while the Group lives.** It is set only when the node is a witness
   at a committed index: `membership_at` reads committed configurations, which are fixed for the
   reasons the prior review lists. It lives only in memory (`driver.h:377`). After a restart, the
   apply-index check rebuilds it, because the membership at the apply index is durable: compaction
   keeps it as the base membership (`core.h:413`), and recovery rebuilds the configurations
   (`core.h:56-77`). The one exception is W1.

   Snapshots: an image moves the apply index only through ActivateInstall, which the application
   performs, so the application received it. A removed node is no replica, so it gets no image.

   A membership change inside one range: a range that holds both the promotion and the removal
   finds the node absent at its last index and a learner at `first - 1`, so the whole range goes
   to the Sink contiguously (`rv_both_in_one`).
3. **Edges.** A range [x..r] that ends exactly at the removal r, with x-1 at or after the
   promotion, is acknowledged: the node is no member at r and a witness at x-1
   (`witness_removal_restart`). A range [r+1..] in a fresh process goes to the Sink only after a
   restart from a checkpoint at r or later (W1). An empty range cannot occur.
   `deliver` (`core.h:1221-1230`) runs only when applied < commit, and it always takes the first
   entry, so `entries.back()` is safe. The code used `entries.back()` before this change too.
4. **G2 is pinned, and each mutant targets the case that kills it.** In witness_promotion:
   - FRONT judges [3..4] at 3, where the node is a learner, so the range goes to the Sink and the
     Sink reaches 4, not 2.
   - COMMIT judges [1..2] at the commit index 6, where the node is a witness, so the range is
     acknowledged and the Sink reaches 0.

   NOFLAG is killed only by [9..9] in witness_removal, where neither index names the node.
   NOPREV is killed only by witness_removal_restart: within one process the flag that [3..4] set
   makes `first - 1` redundant, so only a restart exercises it. The mutant job 7078ef was still
   running when I wrote this, so read it before landing.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| W1 | no | gap | A removed witness may checkpoint at or past its removal. `witness_self()` reads the latest membership, so the witness-bound check is skipped (`core.h:604`, `:750`). After a restart from that checkpoint, neither index names the node, so a late range [T+1..] goes to the Sink. The checkpoint's application state holds nothing past the promotion, so the quest's symptom survives on this path. DRIVER.md states the path. It needs a late AppendRequest to a restarted, removed incarnation that is never a member again and serves no reads. | `driver.h:552-557`, `core.h:600-607`; `rv_post_removal_at` and `rv_post_removal_past` in c7d4af | Owner's call. (a) Keep it, as documented. (b) Have Core refuse a checkpoint whose `through` is at or past this node's own removal, with a driver_test case. (c) Persist a "was a witness" bit in the checkpoint metadata. |
| W2 | no | nit | "Refused by name" is read as "acknowledged under the witness rule", not as a fence or a Reject. That reading matches D9's existing exception, and a fence would fail waiters on a routine replacement. The lead should confirm it against the quest's wording. | journal "Rulings I made"; `docs/DRIVER.md` "The witness transitions, both ways" | None, if the lead agrees. |
| W3 | no | nit | DRIVER.md item 2 says that a range whose last index is "past the removal" counts by the apply index. The code also covers a range that ends exactly at the removal (`witness_removal_restart`). | `docs/DRIVER.md` item 2; `driver.h:555` | Change "is past" to "is at or past". |

## Not run

- When I wrote this review, my probe c7d4af and the contributor's mutant job 7078ef had no
  results. The A2 jobs (suite aec227, TSan c4741a, ASan bf323c, and seeds 713002 and 8e7ebd) and
  CI were queued or running.
- I ran no Driver-level reproduction of a removal through a correct leader. The prior review's G1
  reachability question stays unsettled, and W1 depends on the same window.
- I did not check followers' validation of configuration entries from a faulty leader. That is
  outside the threat model.
