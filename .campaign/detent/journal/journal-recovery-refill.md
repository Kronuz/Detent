# journal-recovery-refill: journal

State: ready for review
Quest: .campaign/detent/quests/journal-recovery-refill.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: fix round 1 (review correctness CHANGES at a6fff53) is code-complete at journal:46a7a31
(pushed); its jobs are queued.

## Next

1. Read fix-round jobs (Evidence, fix round 1) and CI at 46a7a31; the lead re-reviews F1/F3.

## Blocked on

Nothing.

## A1: each pool's share before a crash and after recovery

H hard, C control pool, R replacement pool, `T = H - used` the pool funding the census leaves,
x bytes Control appends drained at the limit, P a checkpoint's peak (P <= R), pack the Driver's
pre-Start Tick (Control, 114 B in the probe). Shares are (control, replacement) available; the
census sees no permits, so a crash cancels any held one.

| How it reached its limit | Before the crash | Census T | After recovery today (control first) | After recovery, rule chosen |
|---|---|---|---|---|
| Control drain | (C-x, R): Replacement(R) admitted | C+R-x | (C, R-x): Replacement(P) refused once x > R-P; Normal refused; wedged | (C, R-x), claim x: Replacement(R) takes x from control, leaving (C-x, R), the pre-crash split |
| Checkpoint mid-publication (permit p, s bytes staged) | (C-x, R-p), p held | C+R-x-s | (C, R-x-s); reclaim of s gives (C, R-x): still short by x | claim x+s; after reclaim the claim is capped at the deficit x; Replacement(R) admitted, (C-x, R) |
| Checkpoint published, superseded G not yet reclaimed | (C-x, R-A) | C+R-x-A | (C, R-x-A); reclaim of G: (C, min(R, R-x-A+G)), as the live ledger would refill | the same, plus a claim of the remaining deficit x+A-G |
| Install (staged image s, Replacement permit and a control cutover permit) | as mid-publication | C+R-x-s | as mid-publication | as mid-publication |
| Checkpoint grew the store (deficit left in replacement by the live control-first refill) | (C, R-y) | C+R-y | (C, R-y): exact | (C, R-y), claim y: a checkpoint may take y from control (more than before the crash) |
| Limit lowered between runs | none under the new limits | any | (min(C,T), rest) | the same split; claim min(R,T) minus replacement's share |
| Over the limit (used > H) | - | 0 | (0, 0) | (0, 0), claim 0 |

The census cannot tell a control deficit from a replacement deficit: (C-x, R) and (C, R-x) have
the same T. So no split computed from T matches every pre-crash ledger.

Rules weighed:

1. Replacement first in the constructor. Matches the Control-drain row exactly, but leaves control
   max(0, T-R) where today it has min(C, T): every store with T < R + pack has its pre-Start Tick
   refused, among them lowered-limit and grown stores that open and checkpoint today (any
   C + P <= T < R + pack). It narrows what opens. Rejected.
2. Keep pool ledgers across a crash. The census sees only bytes; keeping the split means a durable
   record per Control append and a format change outside Admission. Rejected.
3. Replacement may always draw its deficit from control. Fixes this and the live grown-store
   shape, but changes live admission, outside "only the post-recovery split changes". Rejected.
4. Chosen: a recovery claim. The constructor keeps today's control-first split, so every store
   opens exactly as today (Control and Normal admission unchanged), and records the claim:
   what replacement-first funding would give replacement minus what it got, `min(R, T) - ra`.
   A Replacement permit (or its growth) spends its pool first, then up to the claim from
   control's available bytes; claimed bytes become replacement's. Refill caps the claim at the
   replacement pool's deficit, so it ends when replacement is whole; a ledger never recovered has
   claim 0 and behaves as today. Post-recovery, Replacement admits what either split would.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | this table and the rule above |
| A2 | job 261124-204657-f2e321 (`journal_admission_test`, recovery + claim, at a6fff53); fails-without 261124-204700-cfc2f7 (`recovery` at 10cd1e1: the test over main's admission.h, Replacement(R) refused in both cases) |
| A3 | journal `docs/mechanisms/admission.md` (Recovery: the rule and its event table), at a6fff53 |
| A2, fix round 1 | at journal:46a7a31: 261124-213010-aaea95 (whole admission_test); 261124-213014-836945 (`short_schedules`, F1); 261124-213017-d8c48c (`claim`, F3); fails-without 261124-204700-cfc2f7 (unchanged, test commit 10cd1e1) |
| F1 mutants (fails-without, `test/admission_mutants.py NAME` at 46a7a31) | cap-ignores-held 261124-213022-4bd2fc; take-ignores-control 261124-213026-23aa96; grow-unrecorded 261124-213029-039f92; lacking-ignores-held 261124-213033-a6b133 |
| F3 mutant (fails-without) | claim-is-deficit 261124-213037-283680 |
| A4, fix round 1 | suite 261124-213046-c80acd (`campaign verify --for A4` at 46a7a31); integration 261124-213048-5a3b35 (consensus main be18ad9 driver_pressure, recovered_over_limit, core_deadline, worker; journal pinned at 46a7a31); CI on the pushed branch |
| A4 | suite 261124-204704-cc5d9e (`campaign verify --for A4`); integration 261124-204721-cbd3e6 (consensus:d03a71b driver_pressure, recovered_over_limit, core_deadline and worker, journal pinned at a6fff53 via `--ref JOURNAL`); CI on the pushed branch |

Probe read: 261124-202348-6f464f (review F2, both cases reproduce at consensus:d03a71b, journal:9e11713).

## Result

journal:10cd1e1 the test alone; journal:e57078d the rule in `include/journal/admission.h`;
journal:a6fff53 `docs/mechanisms/admission.md`. A census keeps the control-first split (every store
opens as today) and gives replacement a claim on control's available bytes,
`min(R, T) - replacement_available`; a Replacement permit or its growth spends its pool, then the
claim; a settlement keeps what it took, a cancellation returns it as far as control lacks it,
refill caps the claim at replacement's deficit. Contract kept: no public API change; a ledger never
short after its census admits exactly as before.

Local (not evidence): `admission_test` under ASan/UBSan, 4125 checks, 0 failures; five local
mutants of the rule (no census claim, no grow claim, no cancel return, no lacking cap, no refill
cap) each fail named `claim`/`recovery` checks.

Leaves open: ARCHITECTURE.md (outside the file set) does not link the new doc; the live
grown-store shape (replacement short with no crash) is unchanged, as the contract asks.

Not a session of its own: this ran as a subagent, not `detent · contributor journal-recovery-refill`.

## Fix round 1 (review correctness, CHANGES at a6fff53)

- F1 (blocking): class = the claim's transitions whose limits `schedules()` never reached (refill
  cap, take bound, growth record, cancel's lacking). `short_schedules` in `test/admission_test.cc`:
  3000 seeds x 200 steps over random limits and censuses from whole to empty pools (1 in 8 over
  the limit); steps reserve each class, grow, settle, cancel, reclaim, close + new census. After
  every step: outstanding/tickets match the live permits, `ca + Σ control peaks <= C`,
  `ra + Σ replacement peaks <= R`, the hard limit, no taint, the census's control share is
  `min(C, T)`, and reserve/grow/cancel episodes round-trip stats. Public observables only (no
  test accessor; the contract keeps the API). Local run under `quiet light` with the reviewer's
  `run.sh mutants`: all ten mutants killed by admission_test; the four F1 ones each by
  `short_schedules` (cap-ignores-held: replacement pool bound; take-ignores-control: subtraction
  underflow; grow-unrecorded: round trip; lacking-ignores-held: control pool bound).
- F3: `claim` adds census (950, 95), so T = (50, 5) < C, a reclaim of (100, 10), then
  Replacement(150, 15) refused and Replacement(100, 10) admitted taking exactly (50, 5). Kills
  claim-is-deficit.
- F2 (lead ruling, keep the trade): `docs/mechanisms/admission.md` Recovery states it, naming
  consensus `request_install`'s Control cutover permit (worker.h) that can return Pressure where
  the snapshot's Replacement used to be refused; no store loses an operation it has today.
- F4: outside the file set; the lead adds the ARCHITECTURE.md link.
- Commits: journal:114cd67 (tests, mutant script), journal:46a7a31 (doc).

## Rulings I made

- Rule 4 (recovery claim), from A1. It keeps every store's open; rules 1 and 3 were not taken
  because 1 narrows what opens and 3 changes live admission.
- Mutant fails-withouts use an in-tree `test/admission_mutants.py` (exits with the plain
  test's status; 126 when the mutant does not build), not `test/mutants.py`, which is outside the
  file set and exits 0 on a kill. The job slot's `checkout --force` resets the mutated header.
- Integration moved from consensus:d03a71b to consensus main be18ad9 (the over-limit quest
  landed; d03a71b is its ancestor).
- `admission_test` "pressure refuses ... unfunded replacement work" (T=50 < R) changes: after
  recovery a Replacement may now spend the claim (control's bytes), as rule 4 intends.
