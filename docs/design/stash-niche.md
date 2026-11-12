# Does stash win on its own shape?

Quest `stash-niche` (campaign `detent`). `wheel-evidence` (docs/design/wheel-evidence.md) measured
Kronuz/stash as a timer wheel (arm, re-arm and cancel on a loop; arms from many threads against
a firing owner) and D70 retired it there in favour of candidate (f). This note asks the fair
question on the workload stash's design fits, before Kronuz/stash is archived: many producers,
one consumer draining in key order, no cancel and no re-arm, keys spread over a wide horizon
(sparse; stash allocates its levels on first use). If stash beats (f) on a use, its README narrows
to that use and it stays; if not, the retirement stands. Library code is read at stash:d249352,
the harness is `async-drivers-bench/stash_niche*`, built on `wheel_vs_stash.cc`'s candidates
unchanged.

## Arms

- **(b) stash as it is**: Xapiand's four levels (`scheduler.h:226-230`, 1 ms resolution, 24 h
  span), `entry*` values, exactly `cand_b` of wheel-evidence. Nothing cancels or re-arms, so
  the armed flag it carries is only ever set. The consumer drains in key order with one walk.
- **(f)**: the single-owner wheel (4,096 slots of 1 ms, 32-byte intrusive entry) with the 64-stripe
  inbox, exactly `cand_f`. No overflow level: a deadline a round or more ahead waits in its slot
  and is skipped until due. The consumer drains the way D4's design drives the wheel: it files
  the inbox, then moves its clock to `next()` (the end of the next occupied slot) and expires,
  until the wheel is empty.

(f) is measured as built. Where it loses because it has no level for a wide horizon, the verdict
says so; a wheel with one is not measured here.

## Workload

One row is a horizon H, N live keys and P producers; a run is one process.

- **Keys.** N distinct entries, each filed once at a key uniform in [1 ms, H - 1 ms] past the base
  (nanoseconds, so 1 ms is the resolution, not the granularity of the keys), from a fixed seed;
  both arms get the same keys. H is 1 s or 24 h; N is 1,000 or 1,000,000; P is 1, 4 or 14. That
  is 12 rows. No cancel, no re-arm.
- **Insert.** P producer threads on a start barrier each file N / P entries back to back
  (`arm_from`); nothing drains meanwhile. **Insert ns** is the median over producers of the
  producer's wall time per entry, as in wheel-evidence's cross-thread score.
- **Drain.** After the producers finish, one consumer thread drains every entry in key order on
  a manual clock (the base is 7,000 s, far below `steady_clock`, so a wheel that read the real
  clock would show): (b) one walk to H + 2 ms, then the `clean` pass the scheduler's loop runs;
  (f) the loop above. **Drain ns** is the consumer's wall time per entry delivered, so (f)'s
  includes filing the inbox into the wheel, every `next()` and every wake that fires nothing.
- **Cost per key** is insert ns + drain ns: the CPU one key costs, a producer's and the
  consumer's, the number a bill follows. Insert and drain are reported on their own.
- **Peak memory**, the counting build (`probe.hh`'s allocator): the structure's live heap after
  the producers finish, per key (the entries and key arrays are the harness's and not counted;
  neither arm allocates while draining, and stash only frees). The timing build also reports
  the peak resident-set growth over the run as a cross-check.
- **Valid** only if every entry is delivered exactly once, no entry is delivered before its
  deadline on the clock the consumer holds, and delivered ticks (key / 1 ms) never decrease.
  A run not drained within 300 s is stopped, reports the entries delivered so far, and counts
  its drain as that elapsed time over those entries (a lower bound on its ns per entry, so the
  ratio is a bound that favours stash's opponent).
- Producers and consumer do not overlap: the filing of the inbox lands in (f)'s drain, where a
  live consumer would overlap it with the producers. The cost per key counts it once either way.

## Noise (D51)

Each row runs 6 interleaved pairs: a pair runs (b) and (f) once each, each in its own process,
the order rotated by pair and by row. The **noise floor** φ of a row is the larger of the two
arms' A/A spreads on cost per key, each the median over consecutive same-arm runs of
|x(i+1) - x(i)| divided by the arm's median.

## The decision rule

Fixed before the first measured run. The rows are `exclusive` jobs at named commits, split by
row where a run is long; a job lasts at most 30 minutes.

1. A row's ratio **r** is the median over its pairs of (f's cost per key) / (b's cost per key);
   r above 1 means stash is cheaper, and "stash is 1.10x better" means r >= 1.10.
2. A row is a **stash win** when r >= 1.10 and r - 1 > φ (the gap clears the noise floor), every
   run valid, and stash's peak heap per key is at most 1.10 times (f)'s. A row is a **stash
   loss** when r < 1.10, or when stash's peak heap per key exceeds 1.10 times (f)'s.
3. A row with r >= 1.10 and r - 1 <= φ is inconclusive: it reruns with 12 pairs, and the rerun
   decides by its point estimate (r >= 1.10), since a floor only widens with more runs.
4. A **use** is a horizon and a key count, with its three producer counts as its three rows.
   **Stash keeps a use** only when all three of its rows are stash wins. Four uses in all.
5. The verdict follows: if stash keeps no use, retiring it stands (D70). If it keeps any,
   its README narrows to exactly the uses it keeps, and it stays. A kept use whose wins come
   with stash losing insert ns is reported with that, and a use won only because (f) has no
   overflow level is reported with that; neither changes the rule's outcome.
6. Peak heap is exact at a given build, one run per arm and row (P = 1 and 14); two runs of
   one arm that differ by more than 1% are reported.

The PLAN.md decision records the verdict; the lead brings it to the owner.

## Results

Not yet measured.
