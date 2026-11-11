# wheel-evidence: journal

State: waiting on jobs 261111-130756-004da8
Quest: .campaign/detent/quests/wheel-evidence.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

All rows in but the memory rerun with (f), 261111-130756-004da8 (exclusive, queued at
df21a772): fill (f)'s row (`MEM_F` in wheel-evidence.md's memory table; expected equal to (e):
32 B entry, 0 heap per arm, about 4 KiB more wheel heap for the stripes), then State "ready for
review". Main merged at 14452cfb.

## Next

1. Fill (f)'s memory row from 261111-130756-004da8; commit.
2. The lead records the A3 decision in PLAN.md and takes the gap to the owner.

## Blocked on

Lead, then owner: the rule gives no pick (see Result). PLAN.md decision for A3 is the lead's to
write; proposed text: "Timer wheel: no candidate met wheel-evidence's rule; (f), the D4 wheel
with a 64-stripe intrusive inbox, misses only the n = 1,000 single-loop bound (1.12x vs 1.10)
and arms 9-17x cheaper than stash from other threads. Owner decides: adopt (f) as the one wheel
(task-wheel builds it; scheduler moves onto it; stash retired as a timer wheel or improved with
(c)'s leaf), or accept a second wheel." Recommendation: adopt (f).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261111-130750-f599af (passed at 512798d3: model + cross-thread tests of (a)-(f), ASan+UBSan and TSan); candidates in `docs/design/async-drivers-bench/wheel_vs_stash.cc` and `wheel_vs_stash_leaf.hh` |
| A2 | 261111-130753-1687cf (single, R=10), 261111-130755-91093a (cross, R=5), 261111-130756-004da8 (mem, queued); first rows 261111-121549-8cd0f8, 261111-121551-0e58b1, 261111-121553-d6c80e (a-e, superseded); all exclusive, stash pinned at stash:d249352 |
| A3 | Rule pre-registered in `docs/design/wheel-evidence.md` at 7a3682c4, before the first measured job (261111-120056-0484f0 smoke, 261111-121549-8cd0f8 first row); PLAN.md decision id pending the lead (see Blocked on) |
| A4 | `docs/design/wheel-evidence.md`: "## The pick" |

## Result

`docs/design/wheel-evidence.md` (Results, The pick, What it means). By the pre-registered rule no
candidate passes; no pick. Single-loop score (ns per entry lifecycle, medians, R=10), n=1k / 1M:
(a) 39.9 / 346.7; (b) stash 1,075 / 1,217 (27x / 3.5x); (c) stash + intrusive leaf 852 / 599
(21x / 1.7x); (d) 42.4 / 388.6 (1.06x / 1.12x); (e) 47.1 / 348.9 (1.18x / 1.01x); (f) 44.5 /
347.2 (1.12x / 1.00x). Cross-thread producer ns per arm, P=1/4/14: stash 157/196/292; (c)
126/157/216; (d) 21/231/1,348; (e) 15/267/1,379; (f) 13.6/11.6/32.6 (0.06-0.11x of stash).
Memory at 1M: prototype 32 B/entry, 0 heap per arm; stash 43.5 B/entry, 27 B heap per re-arm.
(f) misses only n=1k single loop (1.12x vs 1.10), where three source-identical owner paths
(d, e, f) scatter 1.06-1.18x. Recommendation: adopt (f). Not measured: owner wake, other
hosts/architectures, more than 64 producer threads.

## Rulings I made

- Candidate (e) added after the smoke 261111-120056-0484f0 (strict, noisy, not a decision row):
  (d)'s 56-byte entry put its single-loop score 14-31% over (a)'s. (e) keeps (a)'s 32-byte entry
  and links the inbox through `next`, with (c)'s contract (other threads arm unarmed entries).
  The rule's thresholds are unchanged; its unified list grows to (b)-(e); the rows queued at
  67f3f4c3 were cancelled before running and every row runs with (e).
- Single-loop passes visit entries in one random order (D4 went in index order), so a 1M row
  pays the cache misses a real loop does; the numbers are not comparable with D4's table.
- Cross-thread producers read steady_clock every 64 arms, the same for every candidate.
- After the first rows (wheel-evidence.md, "Changes after the first rows"): candidate (f), a
  striped inbox; the cross workload 2M due in 1-50 ms -> 1M due in 1-1,000 ms, since no owner
  (stash's included) kept up; rule 3's R=10 rerun decides by its median ratio. Thresholds
  unchanged; every row reruns.
