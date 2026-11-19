# driver-apply-gap review: correctness

Commit: consensus:9f7bc0cc6e19786c088edb8d1205ae2553433d89
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The seed-399 diagnosis is right and the heal retry is sound. One blocking gap remains. The new
DRIVER.md invariant says the apply index never passes delivery. The Driver decides witness status
from the newest appended membership, so that invariant does not hold while a learner's promotion
to witness is in flight.

## Evidence I relied on

- 261118-173937-ee5fda: seed 399 x 3000 passes with the retry, at 85ed0e6. The merge from main
  (9f7bc0c) touches only `test/transport/groups_test.cc` and `docs/mechanisms/host-transport.md`.
- 261118-173953-5ceffe: the same seed with only `|| replaced` removed fails with the original
  message. Its log shows nodes 1 and 2 at applied 120 with application 120, and node 3 at applied
  120 with application 0.
- 261118-223705-87f86d: the seed-399 trace at the head. Node 2 places "healed" at 6:126; node 3
  (the witness) leads term 7 and its proposals fail; node 1 leads term 8 and places 8:121, so
  6:120..126 were replaced. I read this from the log, not from the journal.
- The A3 jobs (TSan 950de0, ASan 2d847a, and 20 sweep jobs of 500 seeds each) at 85ed0e6. I accept
  them because the later merge does not touch the code they test.

## What I checked myself

Scripts are under `.scratch/detent/review-driver-apply-gap-correctness/` in the home checkout. Both
probe jobs were still queued when I wrote this review.

- 261118-224440-51acaf (`oracle.sh 10000`, which applies `patch_oracle.py`): checks after every step
  and every heal round that a data voter's `state().applied` is at most its application's last
  delivered index, and that no node's apply index passes its commit index. It also counts the seeds
  that re-propose "healed" or commit it twice. It covers seeds 1-10000 x 3000. This is the check
  A1's trace check cannot make (F3).
- 261118-224442-334749 (`mutants.sh`): runs two Driver mutants against the fixed simulation. M1
  makes a data voter acknowledge every range ending at a multiple of 5 itself. M2 reports each
  Placement one term low, which is the field the retry keys on. Both must be KILLED.

Answers to the lead's questions, from the code at 9f7bc0c:

1. **The contributor's conclusion is right.** Seed 399 is odd, so node 3 was founded as a witness
   (`driver_simulation.cc:150` and `:276`). Its "application 0" is the witness rule; nodes 1 and 2
   have application equal to applied. Core moves `applied_` in only four ways:
   - `Applied`, which must equal `delivered_through_`; any other value is fenced (`core.h:650-656`).
   - An install (`core.h:570`).
   - A repair (`core.h:646`).
   - Recovery to the checkpoint base (`core.h:47`, `checkpoint.h:96`, `retained_state.h:96`).

   The Driver acknowledges a range itself only when `witness(g)` is true (`driver.h:829`). With
   fixed membership, as in the simulation, a data voter cannot pass delivery. The original report
   came from the heal oracle, not from Driver bookkeeping.
2. **Re-proposal is safe.** "healed" can commit twice: a node that holds 6:126 can win a later
   term, re-propose, and then commit both entries. That is harmless.
   - `healed_everywhere` judges only `last`, and the safety oracles (`:242` contiguity, `:245`
     sequence, `check_final`) do not depend on the heal loop.
   - Only a higher-term leader can replace a placement, and same-term truncation is not possible,
     so keying the retry on term costs liveness, never safety. The loop re-proposes at most once
     per term.
   - Indices stay monotone across restarts. Boot clears `n.applied` (`:270`), Core restarts at
     base 0 (the simulation never checkpoints), and `:242` requires each range to start at
     size + 1.
3. **The retry does not reduce what the simulation can detect.** It changes only heal liveness
   after a term change. The only thing it can hide is an uncommitted placement disappearing across
   terms, which Raft allows, so the old failure was a false positive. A wrong Placement under a
   steady term still fails, and a wrong term retries for 4,000 rounds and still fails (M2 in
   334749).
4. **Mostly.** 5ceffe kills the retry and seed 399 fails with the exact original symptom, so the
   regression covers the path A2 names. It lives in `driver_simulation`, not `driver_test`, by the
   lead's ruling. Nothing in the suite asserts that the seed still takes the retry branch (F2).
5. **The doc holds for fixed membership only.** See F1.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | DRIVER.md now says, without conditions, that the apply index never passes what the application was delivered, and that "a witness member is delivered nothing". `witness(g)` reads the newest appended membership: `core.h:192` reads `configs_.rbegin()`, and config entries take effect on append (`core.h:341-346`). A learner promoted to witness, the D9 replacement path (`membership_test.cc:466-490`), therefore acknowledges its committed but undelivered ranges below the promotion entry itself as soon as that entry is appended. If a new leader truncates the promotion (`core.h:409`), the node is a data learner whose apply index has passed its application, which is the defect this quest names. It can later be promoted to voter (D3(c) checks log match, not application). The reverse case has the same cause: a witness whose removal entry is appended sends later ranges to its own Sink, starting mid-log. Neither the simulation (fixed membership) nor any test covers this window. I did not reproduce it. | `driver.h:541-546`, `driver.h:829`, `core.h:192`, `core.h:409`, `membership.h:133-134` | Choose one. (a) Judge witness status by the membership in force at the range's last index (`membership_at`, exposed through the Worker). Add a deterministic `driver_test` case: promote a learner to witness, deliver a range below the promotion after it is appended, have a new leader truncate the promotion, and assert that the application received every index up to the learner's apply index. (b) Restrict the DRIVER.md invariant to a fixed witness, name the promotion and removal window as open, and have the lead file a follow-up quest for (a). |
| F2 | no | gap | The seed-399 regression depends on its trajectory. Any change to the Driver, Core or the simulation's draw order can stop it reaching the replaced placement, and it would then pass vacuously. Only 87f86d, run once, shows that the retry fired. | `driver_simulation.cc:504-525`; 87f86d log | Count `replaced` retries for regression seeds and fail when a regression seed takes none. |
| F3 | no | gap | A1's trace check cannot see the Driver's apply index. It compares the simulation's own `applied(N)` calls with `Committed` ends, and `apply()` always passes `n.applying` (`:348-353`), so the check passes by construction. Its awk `hi[]` is also never reset at boot. | 87f86d cmd | 51acaf checks `state().applied` per step instead; cite it in A1 if it passes. |
| F4 | no | nit | The heal loop can pick a witness leader, whose proposals always fail with NotLeader. This is harmless, and the retry handles it on the error branch. | 87f86d log, rounds 1000037-1000045 | None needed. |

## Not run

- I did not run a reproduction of F1 (a Driver-level membership-change test). I did not run TSan
  or ASan myself, CI 44473373074, or seeds beyond 10,000.
- 51acaf and 334749 were still queued when I wrote this review, so read their results before
  ruling. If 51acaf reports "probe: ... passes delivered", or M1 or M2 SURVIVED, then answer 1 or 3
  above is wrong and the verdict needs revisiting.
