# driver-apply-gap review: correctness

Commit: consensus:4b0b214c46c1175b00e909ecdc6a50f9c47d271e
Verdict: APPROVE
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Round 2. This reviews the fix consensus:ee032308e6e9b63afc8d27fa90c9e12f51a6e73a against round 1's
9f7bc0c, at the head 4b0b214, which adds only merges from main (`git diff ee03230 4b0b214` touches
CMakeLists.txt, transport/resolve.h, two transport tests, dns_mutants.sh and peer-dns.md, none of
them Driver or Core code). F1 is closed. The Driver now judges a range by the membership in force
at the range's last index. That entry is committed, so no later term can truncate it, and no
data member's apply index can pass its delivery on any path I could find. Nothing blocks. Four
notes follow. Of them, G1 (DRIVER.md does not name the removal case) is the one I would fold in
before landing.

## Evidence I relied on

- 261119-044225-2f4e74: truncated_promotion and the whole of driver_test pass at ee03230.
- 261119-044228-4bb073 (fails without): the job restores `w.membership()` with a sed, and a grep
  guard exits 0 when the mutant is not applied, so the job cannot pass vacuously. The log shows
  "applied 3, delivered 0" and "each Committed range starts after the last one the application
  received". Both failures are F1's symptom: range [1..3] is acknowledged without going to the
  Sink, so the next range to reach the Sink starts at 4.
- A3 at ee03230: TSan 300372, ASan fa2a93, and seeds 1-10000 x 3000 (b31c95..dbd1d0). I accept
  them because the merges since then do not touch the code they test.
- Round 1's probes 51acaf (apply index <= delivered at every step, seeds 1-10000) and 334749 (M1
  and M2 KILLED), as read in the journal.

## What I checked myself

Scripts are under `.scratch/detent/review-driver-apply-gap-correctness/` in the home checkout:
`r2_probe.sh`, `r2_cases.cc` and `r2_patch.py`.

- 261119-054307-928bfb (`r2_probe.sh`) was queued when I wrote this review. It patches three cases
  into driver_test, with `rpc_entries = 2` so that ranges split where the cases need them, and
  runs them at the head:
  - r2_straddle: a learner is promoted to witness at 4, and its range [3..4] ends exactly at the
    promotion. Expected: the Sink holds only [1..2], and applied reaches 6.
  - r2_restart: the same group reopened with `recover`. Expected: the ranges are contiguous and
    applied equals committed.
  - r2_removal: a crafted AppendRequest commits the witness's removal at 7. The case prints
    whether the reverse case reproduces; it never fails the job.

  The job then builds two mutants of the witness rule: FRONT, which judges at
  `entries.front()`, and COMMIT, which judges at `membership_at(w.committed())`. It runs each
  mutant against truncated_promotion and r2_straddle.

  Expected results: every case exits 0; both mutants SURVIVE truncated_promotion and are KILLED by
  r2_straddle. If r2_straddle or r2_restart fails at the head, the answer to question 1 below is
  wrong and the verdict needs revisiting.

Answers to the lead's questions, from the code at 4b0b214:

1. **F1 is closed on every path I could construct.** A Committed range's last index `b` is at or
   below the durable commit index (`core.h:1221-1230`). `membership_at(b)` reads only the
   configuration entries at or below `b`, which are fixed for four reasons:
   - Truncation erases only from above the commit index (`note_truncate`, `core.h:410`).
   - Compaction erases entries at or below a checkpoint boundary, and that boundary is at or
     below the apply index, which is below any undelivered range (`core.h:413-414`, `:602`).
   - An install clears the configs only while no range is outstanding (`core.h:640`; `deliver`
     is gated on `busy()`).
   - Recovery rebuilds the configs from the retained entries over the base membership
     (`core.h:56-77`).

   The action is dispatched in the same Driver step that produced it, because the Worker refuses
   a new step while its output is pending (`worker.h:675`, `driver.h:666-673`). The cases the lead
   named:
   - A promotion and a removal inside one range: the membership at `b` lacks this node, so the
     whole range goes to the Sink. The application receives a contiguous range and the invariant
     holds.
   - A promotion at exactly `b`: `upper_bound(b)` includes `b`, so the node acknowledges the
     range itself. The promotion is committed, and the incarnation can never again be a data
     member: witness to learner is refused at `core.h:678`, and witness to voter at
     `membership.h:134`.
   - A restart: the Driver replays from the recovered base under the same rule (r2_restart).

   The rule's only exposure is a witness: its removal (G1), and a straddling range (G3).
2. **Yes, it fails for the right reason.** The mutant is exactly the pre-fix line, and both
   checks fail with F1's symptom. The nearby case it misses is the choice of index. FRONT and
   COMMIT judge identically on its ranges [1..3] and [4..4], so the "last index" that DRIVER.md
   states is not pinned by any test (G2).
3. **Yes, it is safe.** Core is single-owner (`core.h:40`), and the Driver calls it on the
   group's loop inside `step`, as it already called `membership()`, so no lock is needed before
   or after the change. The cost per Committed range (not per entry) is one `std::map` lookup
   plus the copy of one Membership into an optional. `membership()` made that same copy before
   the change, so the cost is unchanged (G4). The other callers are inside Core
   (`committed_membership`, `recovered_limits`, `limits_at`, `note_base`, `note_append`,
   `checkpoint_rejection`) and are untouched. The only new external caller is `driver.h:545`.
4. **The witness exception is stated exactly, but the reverse case is not named.**
   DRIVER.md:57-59 and :186-191 match the code. They give no hint of
   driver-witness-removal-sink, and line 190's "its application holds no more entries" is the
   claim that case breaks (G1).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| G1 | no | gap | DRIVER.md does not name the removal reverse case. When this node's removal is in force at a range's end, the Driver sends the range to the Sink of a witness whose application holds nothing past the promotion. DRIVER.md:189-191 says the opposite. From a correct leader the case is narrow. `replicas()` keeps a removed member only until its removal is durably committed, and an AppendRequest carries the durable commit index. However, `peers_` is pruned only in `drive()`, so a response handled between that commit and the next `drive` (`core.h:1023`) may still send the removed node a commit index at or past its removal. I did not settle that. r2_removal forces the case with a crafted AppendRequest. | `driver.h:831`, `docs/DRIVER.md:189-191`, `core.h:326-331`, `:1090`, `:1190`; r2_removal in 928bfb | Add one clause to DRIVER.md naming the case as open under driver-witness-removal-sink (checked by the A4 grep), or leave it to that quest's A3. Tell that quest that its A1 test may need a crafted AppendRequest, because a correct leader barely reaches the path. |
| G2 | no | gap | The "last index" rule is not pinned by a test. FRONT and COMMIT survive truncated_promotion by construction. Both are as safe as the fix for F1, so this does not affect safety. | `driver_test.cc:502-535`; FRONT and COMMIT in 928bfb | Add r2_straddle (`r2_cases.cc`) to driver_test if the lead wants the rule that DRIVER.md states held by the suite. |
| G3 | no | nit | A range [a..b] that straddles a committed promotion at p is acknowledged whole, so the entries a..p-1 below the promotion are never delivered. That is safe, because a witness never again becomes a data member. One consequence follows: a witness may checkpoint at T in [a, p) (`core.h:602-605` allows it), and after a restart from that checkpoint the ranges in (T, p) go to the Sink on top of the undelivered gap, since `membership_at(T)` is the learner's membership. Only a witness's unused application state is affected. Judging at the first index would keep every index below the promotion delivered. I did not probe this. | `driver.h:831`, `core.h:602-605`, `:413` | Owner's call: keep the last-index rule, or switch to the first index together with driver-witness-removal-sink. |
| G4 | no | nit | `Worker::membership_at` copies a Membership for each Committed range. This is the same copy `membership()` made before the change, so the cost did not move. | `worker.h:190` | None needed. A `witness_at(index)` returning a bool would skip the copy. |

Round 1's F2 (the seed-399 regression depends on its trajectory) stays open and non-blocking, as
the journal records.

## Not run

- When I wrote this review, 928bfb was queued, and the suite c94e51 (A5) and the apply-index
  probe rerun 817bdb (A1) were running. Read their results before landing.
- I did not run a witness checkpoint followed by a restart (G3), a Driver-level test of a removal
  through a correct leader (G1's reachability), TSan or ASan, or CI.
