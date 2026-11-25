# journal-recovery-refill: journal

State: working
Quest: .campaign/detent/quests/journal-recovery-refill.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A2: the probe's two cases as `admission_test recovery`, then the rule in admission.h.

## Next

1. A2 test commit alone (fails-without at main + test), then the fix commit.
2. A3 docs/mechanisms/admission.md.
3. A4 suite (`campaign verify --for A4`) and the consensus integration job (`--ref JOURNAL=`).

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
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

Probe read: 261124-202348-6f464f (review F2, both cases reproduce at consensus:d03a71b, journal:9e11713).

## Result

<pending>

## Rulings I made

- Rule 4 (recovery claim), from A1. It keeps every store's open; rules 1 and 3 were not taken
  because 1 narrows what opens and 3 changes live admission.
- `admission_test` "pressure refuses ... unfunded replacement work" (T=50 < R) changes: after
  recovery a Replacement may now spend the claim (control's bytes), as rule 4 intends.
