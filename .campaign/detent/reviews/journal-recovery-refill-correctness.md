# journal-recovery-refill review: correctness

Commit: journal:a6fff53995ce114807a35ae58a810731791aee09
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The rule is correct at this commit: 50.5 million probe checks over random schedules on short
censuses found no violation, and a ledger whole at its census behaves exactly as main's. What
blocks is coverage. The suite pins five of the claim's transitions, and four mutants that each
break a pool bound or the cancel round trip pass it.

## Evidence I relied on

- 261124-204657-f2e321 (A2): `journal_admission_test` at a6fff53, 4125 checks, 0 failures.
- 261124-204700-cfc2f7 (A2 fails-without): `recovery` at journal:10cd1e1 (the test over main's
  admission.h, identical to journal:9e11713's). It fails exactly the two checks "after the crash a
  whole replacement cycle is funded", once per case, for the stated reason; 12 checks, 2 failures.
- 261124-204704-cc5d9e (suite, Release): 106 of 106 passed, `journal_admission` included.
- 261124-204721-cbd3e6 (A4 integration): consensus:d03a71b driver_pressure x4,
  recovered_over_limit reclaims and refuses, core_deadline and worker all passed. I checked that
  the build really used this commit: its CMakeCache has `FETCHCONTENT_SOURCE_DIR_JOURNAL` set to
  the ref checkout, whose HEAD is a6fff53, and consensus declares the content as `journal`.
- 261124-202348-6f464f (F2 probe): A2's two cases match it (64K/512K drained 48K; 256K/64K
  drained 256000).

## What I checked myself

Scripts are in `.scratch/detent/review-journal-recovery-refill-correctness/`. `run.sh probe` and
`run.sh mutants` rerun them over `$JOB_SRC` and `$JOB_REF_MAIN`.

- **261124-205626-3c4850 and its rerun 261124-211152-a55129 (`probe.cc`, `trace.cc`), passed.**
  1. Exhaustive small domain (C up to 6, R up to 8, every used through hard+1). Control's share
     is today's `min(C, T)`. Control(c) is admitted exactly as today. Replacement(P) is admitted
     iff `P <= min(R, T)`, which is what either split admits. A settled cycle plus a reclaim of
     the census deficit leaves both pools whole.
  2. 20000 random schedules over random 2-D limits and censuses, short ones and over-limit ones
     included. Operations are reserve of each class, grow, settle with removals, cancel and
     reclaim. Every operation is checked against invariants that read private state: held and
     outstanding match the live permits; `ca + ch <= C`; `ra + rh <= R`;
     `claim + ra + rh <= R`; the hard bound; no taint; and a ledger whole at its census keeps
     claim 0 forever. Episodes of only reserve, grow and cancel round-trip the whole ledger, the
     claim included.
  3. The doc's interleavings by value: a Control permit holds the rest of control while the
     claimed cycle cancels, and a second census rebuilds the same claim.
  4. A differential trace against main's admission.h, 3000 seeds of 300 operations each. On a
     census that funds both pools it is identical over all operations (906000 lines). On any
     census with no Replacement work it is identical too (906000 lines). On a short census with
     Replacement work, 11065 lines differ, which shows the trace can detect a difference.
- **261124-205628-eb344e (`mutate.py`, ten mutants of the claim; the job "fails" because some
  survive the suite).**

  | Mutant | `admission_test` | probe |
  |---|---|---|
  | settle-returns | killed | killed |
  | cancel-ignores-lacking | killed | killed |
  | cancel-keeps-claim-spent | killed | killed |
  | claim-first | killed | survived |
  | no-census-claim | killed | killed |
  | cap-ignores-held | **survived** | killed (claim passes replacement's deficit) |
  | take-ignores-control | **survived** | killed (subtraction underflow) |
  | grow-unrecorded | **survived** | killed (cancel round trip) |
  | lacking-ignores-held | **survived** | killed (control passes its pool) |
  | claim-is-deficit | survived | survived |

Answers to the lead's questions:

1. **Is the claim a second ledger that can drift?** No. It is one scalar per resource with one
   invariant, `claim <= replacement_pool - replacement_held - replacement_available`. Taking and
   growing lower the claim and the deficit together. A cancellation raises both by what it
   returns. A settlement raises only the deficit. Refill re-caps the claim. Nothing else touches
   it, and the probe holds the invariant across 50M checks. Control is never over-promised: a
   take is bounded by `control_available`, a return by what control lacks, and the book charges
   both sides.

   A Control append the old code admitted can now be refused, but only after a claimed
   Replacement that the old code refused. For example, census (100, 270) with claim 30:
   Replacement(300) leaves control 70, so Control(100) is refused. This is the documented trade
   (F2 below).
2. **Is there a simpler shape?** Rule 1's rejection holds. Take a grown store (C, R−y) with
   y > C − pack. It opens and checkpoints today, and replacement-first funding would give its
   control `T − R = C − y < pack`, so its pre-Start pack would be refused. A recovery-only refill
   order fixes nothing, because `free` is 0 at the limit. A persisted split changes the format.

   The stored claim is also the minimum state. After a settlement the deficit exceeds the claim,
   and that excess must not be claimable (that would be rule 3), so the claim cannot be derived
   from the deficit. At the census it equals `min(R − ra, control_available)` (F3).
3. **Does A2 check values for both cases, and does the fails-without fail for the stated
   reason?** Yes to both. A2 checks the split, the pack, the take, and the pools whole after the
   cycle, by value in both cases. The fails-without fails only the funding check. Of the claim's
   own transitions, four are unguarded (F1).
4. **Does every store that opens today still open?** Yes. The construction split and Control
   admission equal today's (exhaustive probe), and a ledger with no claim is trace-identical to
   main. The Driver pre-Start pack and recovered-over-limit cases pass in 261124-204721-cbd3e6.
   A store that installs today still installs: the claim is spent only where the old code
   refused the Replacement.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | Four of the claim's load-bearing limits are not guarded by the suite. Each mutant passes `admission_test` while breaking a pool bound or the round trip: the refill cap ignoring `replacement_held`, a take not bounded by `control_available`, growth not recorded in `claimed_`, and `lacking` ignoring `control_held`. The suite's only random test, `schedules()`, runs on `census({100, 10})`, which funds both pools, so the claim is always 0 there. Proof standard 3 (an invariant property test where one exists) applies to this new mechanism. | mutant job 261124-205628-eb344e; admission_test.cc `schedules()` | Add an `admission_test` case of random schedules over censuses short of the pools, as in `probe.cc` `schedules()`. After every operation, assert the pool bounds (`control_available + Σ control peaks <= control_pool`, the same for replacement), the hard bound, and no taint. Assert that reserve, grow and cancel episodes round-trip `stats()`. Use a test accessor if the claim bound needs the private state. Its fails-without is `run.sh mutants` reporting these four mutants killed by `admission_test`. |
| F2 | no | nit | During a claimed cycle, Control admission is narrower than the old code's. That code refused the Replacement and kept control whole. The claim drains control first, so in the grown-store and lowered-limit rows control ends shorter than before the crash until a reclaim. One consumer is consensus install: its cutover Control permit (consensus worker.h:431) is reserved after the Replacement and can now return Pressure where `reserve_snapshot` used to be refused. No store that works today loses an operation. The doc states the trade. | probe.cc `interleavings()` (Control(1) refused after the take); docs/mechanisms/admission.md Recovery | The owner's call. Options: keep it (recommended: it is the price of un-wedging F2, and it is documented), or bound the take so control keeps a floor, which needs a consensus-specific pack size in journal. |
| F3 | no | nit | At the census, `min(R, T) − ra` equals `min(R − ra, control_available)`. The mutant `claim = R − ra` survives both the suite and the probe. The two differ only when T < C and a reclaim lands before the Replacement, and no test pins which one the rule means. | mutant claim-is-deficit in 261124-205628-eb344e | Optional: one by-value case (census T < C, reclaim, then Replacement) that pins the documented formula. |
| F4 | no | nit | ARCHITECTURE.md does not link docs/mechanisms/admission.md; the contributor flagged this. | journal ARCHITECTURE.md | Outside the file set; a one-line link when convenient. |

## Not run

- CI at a6fff53 (still running when I wrote this) and sanitizer builds. The contributor's local
  ASan/UBSan run is not evidence.
- Consensus tests beyond the integration job's eight, among them consensus_test,
  shared_worker_test, shared_cluster_test and driver_install. Detent's own pins of journal were
  not exercised.
- No consensus-level probe of the F2 install Pressure path. It is argued from worker.h:431 and the
  probe's ledger-level refusal, and it does not block.
- This review ran as a subagent, not a session titled
  `detent · reviewer journal-recovery-refill correctness`.

## Round 2

Commit: journal:46a7a31177cddf4e16007ecb42ec55246323987a
Verdict: APPROVE
Date: 2026-11-24

The fix round changes only tests and docs. `git diff a6fff53..46a7a31 -- include/` is empty, so
this round re-checks the whole claim mechanism, every event in the doc's table, against the same
code with the new tests.

### Evidence I relied on

- 261124-213010-aaea95 (A2, `admission_test` at 46a7a31): 3650586 checks, 0 failures.
  261124-213014-836945 (`short_schedules`) and 261124-213017-d8c48c (`claim`) passed.
- 261124-213046-c80acd (suite): 106 of 106 passed.
- 261124-213048-5a3b35 (integration): consensus:be18ad9 with journal pinned at 46a7a31 through
  `--ref JOURNAL`; 9 of 9 passed.
- Each fails-without fails at its named check:
  1. 261124-213022-4bd2fc, cap-ignores-held: 19 failures, "replacement never passes its pool".
  2. 261124-213026-23aa96, take-ignores-control: 1 failure, the ledger's own "resource vector
     subtraction underflow" exception. A take beyond control can surface only there.
  3. 261124-213029-039f92, grow-unrecorded: 139 failures, "reserve, grow and cancel round-trip
     the ledger".
  4. 261124-213033-a6b133, lacking-ignores-held: 130 failures, "control never passes its pool".
  5. 261124-213037-283680, claim-is-deficit: 1 failure, "the claim is min(R, T) minus
     replacement's share, not its whole deficit".
- Slot safety: `test/admission_mutants.py` edits the header in the quest's shared slot and does
  not restore it. The runner resets every slot job with `checkout --force` and refuses a dirty
  slot (campaign runner.py `prepare`), so later jobs there run on the commit's own header.

### What I checked myself

- 261124-213549-4b64a1 (`run.sh mutants` at 46a7a31): the new `admission_test` kills all ten of
  my mutants, including the four that survived round 1 and claim-is-deficit.
- 261124-213550-46741c (`run.sh probe` at 46a7a31): 50480414 checks, 0 failures. The traces
  against main are identical where the quest claims no change (906000 lines each on a whole
  census and with no Replacement work); on a short census with Replacement work, 11065 lines
  differ, as they should.
- `short_schedules` checks the public pool bounds against the live permits' peaks, the hard
  limit, taint, and the cancel round trip, over 3000 seeds that include reopens. The F3 case in
  `claim` pins the census formula by value: T < C, a reclaim, then Replacement(150) is refused
  and Replacement(100) takes exactly the claim.
- F2 ruling (keep): admission.md now states the trade and names consensus `request_install`.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Closed: `short_schedules` kills the four mutants. | 261124-213022-4bd2fc, -213026-23aa96, -213029-039f92, -213033-a6b133; 261124-213549-4b64a1 | None. |
| F2 | no | nit | Closed by the lead's ruling to keep the trade, which the doc now states. | docs/mechanisms/admission.md Recovery | None. |
| F3 | no | nit | Closed: the census formula is pinned by value. | 261124-213037-283680 | None. |
| F4 | no | nit | Open, deferred: the lead adds the ARCHITECTURE.md link after landing. | lead's message | After landing. |
| F5 | no | nit | `take-ignores-control` is killed only by the ledger's underflow exception, not by a named check. It is still a kill, and an overdrawn control can show up nowhere else. | 261124-213026-23aa96 | None needed. |

### Not run

- CI at 46a7a31 and the sanitizer builds are CI's.
- Consensus tests beyond the integration job's set, as in round 1.
- This review ran as a subagent, not a session titled
  `detent · reviewer journal-recovery-refill correctness`.
