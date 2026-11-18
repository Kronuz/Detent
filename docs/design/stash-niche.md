# Does stash win on its own shape?

Quest `stash-niche` (campaign `detent`). `wheel-evidence` (docs/design/wheel-evidence.md) measured
Kronuz/stash as a timer wheel (arm, re-arm and cancel on a loop; arms from many threads against
a firing owner) and D70 retired it there in favour of candidate (f), and `Kronuz/wheel` now holds that wheel as a library. This note asks the fair
question on the workload stash's design fits, before Kronuz/stash is archived: many producers,
one consumer draining in key order, no cancel and no re-arm, keys spread over a wide horizon
(sparse; stash allocates its levels on first use). If stash beats the wheel on a use, its README narrows
to that use and it stays; if not, the retirement stands. Library code is read at stash:d249352,
the harness is `async-drivers-bench/stash_niche*`, built on `wheel_vs_stash.cc`'s pieces
unchanged for stash, and on a copy of `Kronuz/wheel`'s `wheel.hh` at wheel:50665c8
(`stash_niche_wheel.hh`) for the wheel.

## Arms

- **(b) stash as it is**: Xapiand's four levels (`scheduler.h:226-230`, 1 ms resolution), `entry*`
  values, `cand_b` of wheel-evidence with one harness choice: the top level has 4,801 slots of
  18 s instead of 4,800. stash measures a level's span from the slot holding the first valid
  key (`StashSlots::horizon`), so 4,800 slots (exactly 24 h) refuse the last seconds of a 24 h
  horizon as `past_horizon` whenever the base is inside a slot; one more slot covers 24 h from
  any base. wheel-evidence's `cand_b` is unchanged and still has 4,800. Nothing cancels or
  re-arms, so the armed flag it carries is only ever set. The consumer drains in key order
  with one walk.
- **(w) the wheel library**: `Kronuz/wheel`'s `timer_wheel` at wheel:50665c8, vendored verbatim as
  `stash_niche_wheel.hh` (D74): 4,096 slots of 1 ms and six upper levels of 64 slots (4.1 s up to
  142 years) that cascade down as the cursor enters their span, a 32-byte intrusive entry, a
  64-stripe inbox for `arm_remote` from other threads. The consumer drives it as its reactor
  would: `expire(manual_base)` files the inbox, then it moves its clock to `next()` (the end of
  the next occupied slot, or the start of the next occupied upper slot's span) and expires,
  until `next()` is `never`.

The wheel-evidence prototype (f) is not an arm of the rows: it has no upper levels, so over 24 h
its `next()` wakes on every 1 ms tick (see the changes below).

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
  (w) the loop above. **Drain ns** is the consumer's wall time per entry delivered, so (w)'s
  includes filing the inbox into the wheel, every `next()`, the cascades and every wake that
  fires nothing.
- **Cost per key** is insert ns + drain ns: the CPU one key costs, a producer's and the
  consumer's, the number a bill follows. Insert and drain are reported on their own.
- **Memory**, the counting build (`probe.hh`'s allocator): **bytes per key** is the entry's size
  plus the structure's live heap after the producers finish, per key, for each arm (the key
  arrays are the harness's; neither arm allocates while draining, and stash only frees). The
  structure's heap alone is reported with it. The timing build also reports the peak
  resident-set growth over the run as a cross-check.
- **Valid** only if every entry is delivered exactly once, no entry is delivered before its
  deadline on the clock the consumer holds, and delivered ticks (key / 1 ms) never decrease.
  A run not drained within 300 s is stopped, reports the entries delivered so far, and counts
  its drain as that elapsed time over those entries (a lower bound on its ns per entry, so the
  ratio is a bound that favours stash's opponent).
- Producers and consumer do not overlap: the filing of the inbox lands in (w)'s drain, where a
  live consumer would overlap it with the producers. The cost per key counts it once either way.

## Noise (D51)

Each row runs 6 interleaved pairs: a pair runs (b) and (w) once each, each in its own process,
the order rotated by pair and by row. The **noise floor** φ of a row is the larger of the two
arms' A/A spreads on cost per key, each the median over consecutive same-arm runs of
|x(i+1) - x(i)| divided by the arm's median.

## The decision rule

Fixed before the first measured run. The rows are `exclusive` jobs at named commits, split by
row where a run is long; a job lasts at most 30 minutes.

1. A row's ratio **r** is the median over its pairs of (w's cost per key) / (b's cost per key);
   r above 1 means stash is cheaper, and "stash is 1.10x better" means r >= 1.10.
2. A row is a **stash win** when r >= 1.10 and r - 1 > φ (the gap clears the noise floor), every
   run valid, and stash's bytes per key (entry plus heap) are at most 1.10 times (w)'s. A row is a **stash
   loss** when r < 1.10, or when stash's bytes per key exceed 1.10 times (w)'s.
3. A row with r >= 1.10 and r - 1 <= φ is inconclusive: it reruns with 12 pairs, and the rerun
   decides by its point estimate (r >= 1.10), since a floor only widens with more runs.
4. A **use** is a horizon and a key count, with its three producer counts as its three rows.
   **Stash keeps a use** only when all three of its rows are stash wins. Four uses in all.
5. The verdict follows: if stash keeps no use, retiring it stands (D70). If it keeps any,
   its README narrows to exactly the uses it keeps, and it stays. A kept use whose wins come
   with stash losing insert ns is reported with that, and a use won on a row where the wheel wakes
   mostly to cascade is reported with its wake counts; neither changes the rule's outcome.
6. Bytes per key are exact at a given build, one run per arm and row (P = 1 and 14); two runs of
   one arm that differ by more than 1% are reported.

The PLAN.md decision records the verdict; the lead brings it to the owner.

## Changes after the smoke

The smoke (261111-200152-81602e, N = 100,000, P = 4, not a decision row) ran the evidence
prototype (f) as the wheel arm. At 24 h it took 141,619 ns per key against stash's 2,375: 82.8M
wakes for 100,000 entries, 82.7M of them empty, because (f) has no upper levels and its
`next()` stops at every occupied 1 ms slot. Stash's heap was 470 B per key against (f)'s 0.7
(its 32-byte entries are held by the caller). The lead ruled two changes (PLAN.md D74) before any
decision row; the thresholds are unchanged and every row runs under them.

1. **The wheel arm is `Kronuz/wheel`'s `wheel.hh` at wheel:50665c8** (arm (w)), not (f). The
   rows measure stash against the wheel that would replace it, and (f)'s wakes are a flaw of
   the prototype, not a cost of the design. The smoke's 24 h run repeats on (w) before any row:
   its wakes should be near the number of occupied slots plus the cascade wakes.
2. **Rule 2's memory gate compares total bytes per key**, the entry each design needs plus its
   structure heap per key, for both arms, instead of structure heap alone, which held every
   intrusive entry against stash.

## Results

Rows at 90309fe6 (the harness and the vendored wheel are unchanged since 107fe6fb), stash pinned
at stash:d249352, wheel:50665c8, on one Intel Core i9-9980HK (8 cores, 16 threads, macOS 15,
Apple clang 17, x86-64 binaries, `-O2 -DNDEBUG`), `exclusive` jobs, 6 interleaved D51 pairs per
row, all 144 runs valid (every entry delivered once, none early, ticks in order, none capped).
Jobs: 1 s x 1,000 keys 261111-231503-9f6729; 1 s x 1,000,000 261111-231504-ab1b64; 24 h x 1,000
261111-231504-105c4b; 24 h x 1,000,000 261111-231505-db629f; memory (strict, counting build)
261111-231503-c6e0f3, -231504-d0ab1d, -231504-a7001e, -231505-46ce45. The checks and the smoke
that preceded them: 261111-213900-917751, 261111-213902-d798e3. The same rows had run once
before at 771a3c5e (261111-225202-d8a30c, -225206-c080d7, -225211-0eb5ec, -225215-abef30; memory
-225204-6df5aa, -225208-e62bf7, -225213-02c592, -225217-6c3043) and were rerun at the branch
head: every row has the same outcome, and every cost per key agrees within 15%, 24 h x 1,000 keys
at P = 14 apart (stash 9,296 then 7,946; its band is 23-24%). Medians over the 6 runs of an arm,
ns per key; **r** is the median over pairs of (w cost) / (b cost), so r above 1 means stash is
cheaper; φ is the D51 noise floor on cost per key.

| Horizon | Keys | P | stash insert / drain / cost | wheel insert / drain / cost | r | φ | time only |
|---|---|---|---|---|---|---|---|
| 1 s | 1,000 | 1 | 236 / 137 / 378 | 14.6 / 34 / 48 | 0.131 | 6.8% | loss |
| 1 s | 1,000 | 4 | 633 / 144 / 774 | 33 / 34 / 68 | 0.087 | 17.5% | loss |
| 1 s | 1,000 | 14 | 4,578 / 155 / 4,744 | 220 / 38 / 264 | 0.049 | 38.1% | loss |
| 1 s | 1,000,000 | 1 | 96 / 257 / 354 | 10.6 / 107 / 117 | 0.336 | 12.8% | loss |
| 1 s | 1,000,000 | 4 | 156 / 251 / 415 | 10.6 / 111 / 123 | 0.302 | 7.1% | loss |
| 1 s | 1,000,000 | 14 | 286 / 256 / 541 | 32.5 / 109 / 144 | 0.261 | 4.2% | loss |
| 24 h | 1,000 | 1 | 598 / 1,201 / 1,799 | 11.7 / 4,044 / 4,057 | **2.300** | 3.1% | **win** |
| 24 h | 1,000 | 4 | 1,124 / 1,200 / 2,370 | 26 / 4,058 / 4,090 | **1.734** | 3.3% | **win** |
| 24 h | 1,000 | 14 | 6,753 / 1,209 / 7,946 | 194 / 4,335 / 4,521 | 0.558 | 23.3% | loss |
| 24 h | 1,000,000 | 1 | 744 / 1,464 / 2,201 | 10.1 / 205 / 215 | 0.098 | 7.6% | loss |
| 24 h | 1,000,000 | 4 | 863 / 1,436 / 2,274 | 11.1 / 207 / 219 | 0.097 | 5.2% | loss |
| 24 h | 1,000,000 | 14 | 1,855 / 1,486 / 3,324 | 33.5 / 212 / 244 | 0.074 | 4.5% | loss |

No row was inconclusive (rule 3 reran nothing): every ratio is either below 1.10 or clears its
floor by a wide margin. The wheel's insert is 9 to 71 times cheaper than stash's in every row
(stash's grows with P, 96 to 6,753 ns), and its drain is cheaper in 9 of 12 rows (not at
24 h x 1,000 keys).

**Memory** (bytes per key, entry plus structure heap; exact: repeats are identical, and a row's
P = 1 and 14 differ by at most 1.1%). The gate fails stash in every row, as D74 reworded it.

| Horizon | Keys | stash entry + heap | wheel entry + heap | stash / wheel |
|---|---|---|---|---|
| 1 s | 1,000 | 16 + 213.8 = 229.8 | 32 + 65.9 = 97.9 | 2.35x |
| 1 s | 1,000,000 | 16 + 27.4 = 43.4 | 32 + 0.07 = 32.1 | 1.35x |
| 24 h | 1,000 | 16 + 706.8 = 722.8 | 32 + 65.9 = 97.9 | 7.38x |
| 24 h | 1,000,000 | 16 + 366.2 = 382.2 | 32 + 0.07 = 32.1 | 11.92x |

The wheel's 1,000-key figure is its fixed 66 KiB of slot heads over few keys. Stash's resident
set grows 351 MB over the 24 h x 1,000,000 run (the wheel's 0.2 MB), 26 MB at 1 s.

## Verdict

**Retiring stash stands (D70): stash keeps no use.** By the rule, stash keeps a use only if all
three of its producer-count rows are stash wins, and its memory is also at most 1.10x the
wheel's; it wins no use on either count.

- **1 s, 1,000 and 1,000,000 keys: lost on every row.** The wheel is 3 to 20 times cheaper per key
  (r = 0.05 to 0.34), 2.2 to 4.2 times on drain alone, and stash uses 1.35x to 2.35x the bytes.
- **24 h x 1,000,000 keys: lost on every row**, r = 0.07 to 0.10: the wheel's 1,015,041 wakes
  (20,861 of them empty) drain in 205-212 ns per key against stash's 1,436-1,486, and its
  inserts cost 10-34 ns against 744-1,855.
- **24 h x 1,000 keys: the only place stash is ahead, and not on a whole use.** Stash is cheaper
  at P = 1 (r = 2.30) and P = 4 (r = 1.73), both clearing their floors; at P = 14 it loses
  (r = 0.558) because its insert grows with contention to 6,753 ns. Its drain is 3.4x the
  wheel's at every P (1,200-1,209 ns against 4,044-4,335), the wheel waking 2,263 times for 1,000
  keys (1,263 empty, the cascades of a sparse 24 h horizon). The memory gate fails it as well
  (722.8 against 97.9 B per key, 7.4x).

The verdict does not depend on D74's gate: with the gate ignored ("time only", the last column)
no use has all three rows won either, and the gate then adds only that stash's memory is 1.35x
to 11.9x the wheel's. What stash's one winning region says, for the owner and not under the
rule: stash's drain is better than the wheel's on a sparse key set over a wide horizon (3.4x at
1,000 keys over 24 h, where the wheel pays its cascades and empty wakes), and its inserts are
not, because producers share its slots. A single-producer or few-producer sparse timeline is
the only shape in these rows where stash beat the wheel, and it did so by 1.7x to 2.3x on cost
and used 7.4x the memory; that is not a use the rule registered and not one the rows prove.

**Not measured:** producers overlapping the consumer (the wheel's filing would then overlap the
inserts), Linux, key distributions other than uniform, N between 1,000 and 1,000,000 or above
it, more than 14 producers, a consumer that must also wait for the next key (`next()` and a
wake), and a wheel whose cascades are cheaper on a sparse wide horizon.
