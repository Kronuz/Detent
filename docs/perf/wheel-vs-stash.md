# Kronuz/wheel against stash

The landed `wheel::timer_wheel` (`wheel.hh` at wheel:bfd72bc, fetched) and stash (stash:b7d8876, the
pin scheduler declares) on the same workloads, in one build of `docs/design/async-drivers-bench`.
Every number below is a median over interleaved rounds from an `exclusive` job (memory rows: a
`light` job, deterministic counts) on one machine. Stash is the reference `b`; `b2` runs stash again
as the A/A twin; `w` is the wheel. No prototype or candidate arm appears.

## Summary

1. Whole-life cost of a timer (arm, re-arm, cancel, arm, expire): the wheel takes 42 ns against
   1,110 ns at 1,000 entries (0.037x) and 407 ns against 1,231 ns at 1,000,000 (0.32x).
2. Arming from another thread: 14.8, 12.1 and 31.8 ns against 148, 199 and 307 ns at P = 1, 4, 14
   (0.097x, 0.061x, 0.103x).
3. Fire-and-forget keys over 24 h: at 1,000,000 keys the wheel costs 211-239 ns per key against
   2,237-3,315 ns (0.07-0.095x), and holds 32 bytes per key against 382.
4. Where stash wins: a cancel is a flag clear in stash (0.66 ns against 3.7 ns) and its cost moves
   to expire; 1,000 fire-and-forget keys at P = 1 and 4 cost the wheel 2.3x and 1.8x stash's total
   (the wheel's wake-per-key drain); and at P = 4 and 14 the worst fire lateness is 3.4 and 4.5 ms
   in the wheel against 1.02 ms in stash.

## Setup

Host Darwin x86_64, Intel i9-9980HK (16 hardware threads), Apple clang 17.0.0, `-O2 -DNDEBUG`
(memory rows `-DCOUNT_HEAP`, same flags otherwise). The jobs ran at Detent commit
`2e69b9f16b3c` with `--ref wheel` and `--ref stash` at the pins above; each log's `pin` line
records them. Round headers record `speed_limit=100` in every round and a load average of 2.4-4.9
(the harness's own threads and other background work; `exclusive` held the machine against other
jobs). `ghz0` and `ghz1` (the core clock around each run) are in the logs; arms share the same
clock range.

Each round runs every arm once, each run in its own process, in an arm order rotated per round.
A ratio is the median over rounds of `w` / `b` in that round, with a distribution-free 95% interval
from order statistics (D77, `bench/wheel_bench_summary.py`). The A/A band is `b2` / `b` the same
way, the farther end of its interval from 1.00 on the deciding metric (score, producer ns, cost
ns). A ratio whose interval stays outside the band is a difference; inside it, a wash. Every ratio
below is outside its band by a wide margin; the exceptions are named.

Correctness first. Both arms match the harness's reference model (20,000 random arm, re-arm,
cancel and advance rounds, 512 entries) and the cross-thread checks (every entry fires exactly
once, never early) under ASan+UBSan and TSan: test job `261112-131222-a47299`, passed. Every timing
run reports `valid=1`, `early=0`, `twice=0`, `missing=0`, and every niche run delivers all keys in
order (`disordered=0`); no run is capped.

## Single thread: arm, re-arm, cancel, expire

Job `261112-134519-339b0d`, 40 rounds, manual clock. One repetition arms every entry, re-arms
every entry, cancels every entry, arms every entry again, then expires in 1 ms steps until all
fired; each pass visits the entries in one random order. ns per entry operation.

| n | metric | stash `b` | wheel `w` | w/b median [95%] | A/A b2/b | apples to apples |
|---|---|---|---|---|---|---|
| 1,000 | arm | 134.23 | 6.22 | 0.046 [0.045-0.048] | 0.997 [0.987-1.009] | yes |
| 1,000 | re-arm | 105.31 | 8.86 | 0.084 [0.081-0.086] | 0.994 [0.986-1.014] | no |
| 1,000 | cancel | 0.66 | 3.69 | 5.593 [5.492-5.803] | 1.000 [0.984-1.033] | no |
| 1,000 | expire | 870.79 | 23.19 | 0.026 [0.025-0.027] | 0.998 [0.985-1.021] | no |
| 1,000 | whole life (score) | 1,110.37 | 42.00 | 0.037 [0.036-0.039] | 0.997 [0.986-1.019] (band 1.9%) | yes |
| 1,000,000 | arm | 179.94 | 140.93 | 0.777 [0.721-0.793] | 0.979 [0.948-1.004] | yes |
| 1,000,000 | re-arm | 186.06 | 127.31 | 0.702 [0.648-0.719] | 0.992 [0.973-1.028] | no |
| 1,000,000 | cancel | 5.89 | 28.77 | 4.887 [4.592-5.173] | 1.003 [0.972-1.116] | no |
| 1,000,000 | expire | 849.21 | 107.26 | 0.122 [0.119-0.132] | 1.015 [0.967-1.040] | no |
| 1,000,000 | whole life (score) | 1,230.51 | 407.14 | 0.323 [0.312-0.336] | 0.995 [0.975-1.028] (band 2.8%) | yes |

Apples to apples, per row:

1. Arm: yes. Both file one entry at a deadline; stash into its slot tree, the wheel into a level.
2. Re-arm: no. The wheel moves the entry; stash files it again and leaves the earlier filing in
   place, stale. The re-arm row is the cost of filing, not of the same operation.
3. Cancel: no. Stash clears a flag in the entry (0.66 ns at 1,000, 5.9 ns at 1,000,000, a cache
   miss); the wheel unlinks the entry from its slot (3.7 ns and 28.8 ns). The stale filing stays in
   stash until expire walks it.
4. Expire: no. Per entry fired, stash's walk also pays for every stale filing: each entry holds
   three filings per repetition (arm, re-arm, arm again) and one fires, so about two of three
   walked filings are stale. The wheel's expire touches only live entries.
5. Whole life: yes. Per entry and repetition, the sum of the four rows covers the same work
   (arm, re-arm, cancel, arm, expire to fire), so the score compares the two designs end to end.
   This is the row to quote.

The wheel wins the whole life at both sizes. At 1,000 entries everything fits in cache and the
wheel's flat levels cost 6-9 ns per arm or re-arm (stash 105-134 ns); at 1,000,000 both are bound
by cache misses on random-order entries and the gap narrows to 0.78x on arm and 0.70x on re-arm.
Stash wins the cancel row at both sizes (by 5.6x and 4.9x); the wheel pays that cancel as a real
unlink and then has nothing stale to pay for later.

Repetitions differ by arm because each run lasts at least 100 ms of timed passes (in the last round the wheel
ran 2,199 repetitions at 1,000 entries, stash 1,000); every figure is per entry operation.

## Cross-thread arm

Job `261112-134522-786aad`, 40 rounds, `steady_clock`. P producer threads arm 1,000,000 entries
(999,992 at P = 14) at now plus 1-1,000 ms; one owner fires them. `producer_ns` is the median
producer's elapsed time per arm (`arm_remote` for the wheel).

| P | stash `b` | wheel `w` | w/b median [95%] | A/A b2/b | apples to apples |
|---|---|---|---|---|---|
| 1 | 148.20 | 14.77 | 0.097 [0.092-0.101] | 1.039 [0.982-1.079] (band 7.9%) | yes |
| 4 | 198.91 | 12.12 | 0.061 [0.057-0.063] | 0.999 [0.970-1.012] (band 3.0%) | yes |
| 14 | 306.60 | 31.76 | 0.103 [0.102-0.104] | 0.991 [0.959-1.008] (band 4.1%) | yes |

Apples to apples: yes. Every producer arms entries it owns from its own thread while the owner
fires, and every entry fires exactly once and never early in both arms. The row measures the
producer's cost; the owner's cost of filing what the wheel's inbox holds is not in it (the niche
rows below include it).

Fire lateness (`max_late_ms`, the worst fire after its deadline in a run; median over rounds,
plain numbers, not a paired ratio):

| P | stash `b` | wheel `w` |
|---|---|---|
| 1 | 1.02 (1.00-20.96) | 0.20 (0.14-0.94) |
| 4 | 1.02 (1.01-14.46) | 3.42 (2.44-4.83) |
| 14 | 1.02 (1.01-1.66) | 4.46 (2.66-5.52) |

Stash's walk stops one 1 ms tick short of now, so its worst is the tick. The wheel is the earlier
at P = 1 and later at P = 4 and 14, by 3.4x and 4.4x; its whole range at P = 4 and 14 is above
stash's median. The owner spins on `steady_clock` with P + 1 threads on 16 hardware threads, in both arms; the cause of the wheel's tail at P > 1 is not isolated here. Stash wins this row.

## Fire-and-forget keys over 24 hours (stash-niche)

Job `261112-134524-cd4fc1`, manual clock, 40 rounds at 1,000 keys and 24 rounds at 1,000,000. P
producers file N keys uniformly over a 24 h horizon (no cancel, no re-arm); one consumer then
drains them in key order. Stash uses Xapiand's four levels with the top level one slot wider
(4,801 x 18 s) so the full 24 h fits. `insert_ns` is the producer's median elapsed time per key,
`drain_ns` the consumer's time per key, `cost_ns` their sum.

N = 1,000,000 (24 rounds), ns per key:

| P | metric | stash `b` | wheel `w` | w/b median [95%] | A/A b2/b on cost |
|---|---|---|---|---|---|
| 1 | insert | 746.3 | 10.4 | 0.014 [0.012-0.014] | |
| 1 | drain | 1,482.3 | 201.7 | 0.137 [0.132-0.140] | |
| 1 | cost | 2,237.3 | 211.3 | 0.095 [0.093-0.097] | 0.996 [0.967-1.028] (band 3.3%) |
| 4 | insert | 859.9 | 10.0 | 0.012 [0.011-0.012] | |
| 4 | drain | 1,491.0 | 202.7 | 0.135 [0.132-0.142] | |
| 4 | cost | 2,343.2 | 212.9 | 0.090 [0.089-0.094] | 0.986 [0.974-1.015] (band 2.6%) |
| 14 | insert | 1,811.3 | 33.4 | 0.018 [0.017-0.019] | |
| 14 | drain | 1,484.7 | 205.0 | 0.136 [0.130-0.142] | |
| 14 | cost | 3,315.4 | 238.6 | 0.071 [0.070-0.075] | 1.004 [0.991-1.010] (band 1.0%) |

N = 1,000 (40 rounds), ns per key:

| P | metric | stash `b` | wheel `w` | w/b median [95%] | A/A b2/b on cost |
|---|---|---|---|---|---|
| 1 | insert | 660.8 | 12.9 | 0.020 [0.018-0.021] | |
| 1 | drain | 1,326.9 | 4,525.3 | 3.457 [3.350-3.584] | |
| 1 | cost | 1,988.3 | 4,538.4 | 2.310 [2.227-2.383] | 1.001 [0.985-1.012] (band 1.5%) |
| 4 | insert | 1,200.8 | 29.3 | 0.023 [0.022-0.026] | |
| 4 | drain | 1,278.0 | 4,250.9 | 3.456 [3.367-3.495] | |
| 4 | cost | 2,499.9 | 4,276.0 | 1.764 [1.631-1.826] | 1.010 [0.979-1.043] (band 4.3%) |
| 14 | insert | 7,221.5 | 174.6 | 0.025 [0.023-0.027] | |
| 14 | drain | 1,161.5 | 4,136.1 | 3.540 [3.465-3.598] | |
| 14 | cost | 8,445.3 | 4,334.9 | 0.529 [0.491-0.560] | 1.003 [0.964-1.081] (band 8.1%) |

Apples to apples, per row:

1. Insert: yes. P producers file the same N keys at the same deadlines, no other operation.
2. Drain and cost: plain numbers beside each other, same keys and same output (every key
   delivered once, in key order), but not the same call pattern. Stash has no wake hint, so the
   consumer drains it with one walk to the horizon's end. The wheel's consumer does what an event
   loop does: expire, then sleep until `next()`, then expire again; the logs count the wake-ups
   (1,015,041 at N = 1,000,000, about one per key; 2,263 at N = 1,000, of which 1,263 find
   nothing due). Cost sums insert and drain for the same keys, so its ratio is
   the whole job's ratio under those two patterns.

At 1,000,000 keys the wheel wins every row: cost 10-14x lower, insert 55-83x lower, drain 7x lower. At 1,000 keys the wheel's drain is 3.5x
stash's (about 2 us per wake-up over 2,263 wake-ups, against stash's one walk), which makes the
total 2.3x stash's at P = 1 and 1.8x at P = 4: stash wins. At P = 14 the producers' contention on
stash (insert 7.2 us) outweighs it and the wheel's total is 0.53x. Why a wake-up costs about 2 us
here against about 0.2 us at 1,000,000 keys is not isolated (each run is a few milliseconds in a
fresh process); it is a cold-path effect at a size where the whole job takes 4 ms.

## Memory

Job `261112-134528-c24e96` (counting build; each arm twice, identical runs). Bytes are the
structure's allocator total (`COUNT_HEAP`), plus the entry.

| row | stash `b` | wheel `w` | apples to apples |
|---|---|---|---|
| entry, bytes | 16 | 32 | no: the entry types differ by design |
| bytes per entry, n = 1,000 (entry plus heap) | 269.25 | 97.55 | yes |
| bytes per entry, n = 1,000,000 | 43.53 | 32.07 | yes |
| heap per armed entry, n = 1,000 | 244.00 | 0.00 | yes |
| heap per armed entry, n = 1,000,000 | 27.52 | 0.00 | yes |
| heap per re-arm, n = 1,000 / 1,000,000 | 101.30 / 27.20 | 0.00 / 0.00 | no: stash files again |
| heap after expire (structure only) | 74,784 | 65,552 | yes |
| 24 h keys, N = 1,000: bytes per key (entry plus heap) | 722.82 | 97.88 | yes |
| 24 h keys, N = 1,000,000: bytes per key | 382.21 | 32.07 | yes |

The wheel allocates its 65,552 bytes at construction and nothing per entry; the per-entry cost is
its 32-byte intrusive entry, so 97.55 bytes at 1,000 entries is that entry plus the fixed block
spread over them, and 32.07 at 1,000,000. Stash grows slots as entries arrive: 244 heap bytes per
entry at 1,000 and 27.5 at 1,000,000, and about 366 bytes per key when 1,000,000 keys spread over
24 h (the `rss_growth_mb` column: 350.9-351.7 MB against 0.2 MB across the drain). At 1,000,000 24 h
keys that is 366 MB of heap the wheel does not use, which is the figure that matters for a
long-horizon fire-and-forget workload (the same at P = 1 and P = 14). Per entry the wheel is never larger than stash here; empty, it is: the wheel's 65,552 bytes
against stash's 9,248 at construction (`wheel_heap` in the log).

## Where stash wins or it is a wash

1. Cancel: stash 0.66 ns and 5.9 ns against the wheel's 3.7 ns and 28.8 ns. Not the same work
   (see the single-thread rows); the whole-life row, which includes the cost stash defers, favors
   the wheel at both sizes.
2. Fire lateness at P = 4 and 14: stash's worst fire is 1.02 ms, the wheel's 3.4 and 4.5 ms (0.20
   ms at P = 1). Open: not diagnosed here.
3. 1,000 fire-and-forget keys at P = 1 and 4: stash's total is 0.43x and 0.57x the wheel's. The
   consumer pattern differs; see above.
4. Empty structure: stash holds 9,248 bytes, the wheel 65,552; a process with a handful of timers
   pays the wheel's fixed block.
5. No timing row is a wash. The widest A/A bands are 8.1% (niche cost, P = 14, N = 1,000) and 7.9%
   (cross, P = 1); the closest wheel result to a band is 0.78x on arm at 1,000,000 entries,
   which is outside it.

## Harness

`docs/design/async-drivers-bench/`: `wheel_vs_stash.cc`, `stash_niche.cc`, their scripts
(`wheel_vs_stash.sh`, `stash_niche.sh`, `wheel_vs_stash_common.sh`) and summaries
(`wheel_vs_stash_summary.py`, `stash_niche_summary.py`).

1. The targets build the fetched library, no copy and no candidate arm. `CMakeLists.txt`
   declares `wheel` at wheel:bfd72bc13395e4a232701e752be97ccd236530fc and `stash` at
   stash:b7d887678a2149e38fd4bc9ec4491e414d76a81f with `FetchContent`; `-DBENCH_WHEEL_VS_STASH=ON`
   builds `wheel_vs_stash` and `stash_niche` and nothing else, linked to `wheel::wheel` and
   `stash::stash`. The sources include `<wheel.hh>` with angle brackets, so no header in the
   directory shadows the library; a job passes its pinned checkouts (`--ref wheel=...`,
   `--ref stash=...`, `FETCHCONTENT_SOURCE_DIR_*`) and every log's `pin` line prints the commits
   read from those checkouts.
2. The arms are `b` (stash as is, `stash.h` in Xapiand's four levels, entry pointers as values,
   cancel clears a flag), `b2` (`b` again, the A/A twin) and `w` (`wheel::timer_wheel`, any thread
   arms with `arm_remote`). The harness is shared by both targets; `stash_niche` includes
   `wheel_vs_stash.cc`'s pieces unchanged.
3. The statistics are the wheel's own, `bench/wheel_bench_summary.py` in the fetched checkout,
   loaded by path (D77): the harness has no copy of it either.
4. The prototype `wheel.hh` and the design-record tests and probes that include it (`loops.hh`,
   `wheel_test.cc`, `placement_test.cc`, `driver_cost.cc` and the others) stay in the directory as
   the async-drivers design record (D83); none of them is a wheel-vs-stash measurement, and no
   number above comes from them.
5. Commands: `wheel_vs_stash.sh test|smoke|single R|cross R|mem` and `stash_niche.sh test|smoke|row
   HORIZON_MS N R|mem HORIZON_MS N`. The jobs above ran `single 40`, `cross 40`, `row 86400000
   1000 40` then `row 86400000 1000000 24`, and `mem` of each script. Linux is not measured here.

## Jobs

| rows | job | level | commit |
|---|---|---|---|
| model and cross checks (ASan+UBSan, TSan) | `261112-131222-a47299` | light | 43f5dbf1 |
| single thread | `261112-134519-339b0d` | exclusive | 2e69b9f16b3c |
| cross-thread | `261112-134522-786aad` | exclusive | 2e69b9f16b3c |
| 24 h fire-and-forget (N = 1,000 and 1,000,000) | `261112-134524-cd4fc1` | exclusive | 2e69b9f16b3c |
| memory (both harnesses) | `261112-134528-c24e96` | light | 2e69b9f16b3c |
