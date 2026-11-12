> Status: RESULTS (2026-11-12) Pre-registered decision rules for the three measurements namespace-delivery D6 owes before S5.2: T9 (α), T4(7) (`completion_sync_probe`) and the period-64 wire run, and their verdicts. Maintainer: Detent campaign, quest namespace-measure.

# Namespace measurements: T9, T4(7) and the roll-up period

Design: [namespace-delivery.md](../design/namespace-delivery.md) D6, the T4 and T9 rows of "Gate rows", and "Roll-up period 64, staggered" under Alternatives.

## Rules

These rules were committed before any measurement job ran. A verdict follows its rule; a result the rule did not foresee is reported as such, and the rule is not edited after the fact.

### Common

1. **Host.** The reference host is the Intel Core i9-9980HK (16 threads, 32 GiB, macOS, APFS on the internal NVMe). The Linux platform is the local QEMU VM (Ubuntu 24.04, kernel 6.8, 8 vCPUs, 16 GiB, ext4 on virtio, `cache=none`), driven under a host hold with `QUIET_VM` pinning its threads. No AWS and no rented machine.
2. **Builds.** Release builds (`-O3 -DNDEBUG` on macOS, `-O2` or better on Linux), x86_64, checked with `file` on every binary that times anything. Detent at this quest's branch commit (its `src/` equals `main`'s), completion at `completion:ab0f551` for the probe.
3. **Repetition.** Every number that decides anything comes from an `exclusive` job on the reference host, or a `strict` job with `QUIET_VM` in the VM, each under 30 minutes. A point is the median of its runs (T4(7): the rounds of its job, D55; T9: five runs on the 100K corpus, D85); its spread is the range (max - min). A difference counts only when it exceeds both arms' spreads. T9's 5M corpus gets one budgeted `exclusive` round, which confirms the 100K result and decides only when it disagrees with it (T9 rule 6).

### T9: the cut point, α

**What is timed.** The agent at a prepared 5M store (the corpus `corpus-5m.bin`, SHA-256 recorded with each job, published as a generation in a fresh store directory) is handed one answer as wire bytes and timed from the first byte given to its decoder until the new generation is published and durable (the store reports `Published`): decode and validation, the plan (delta plan for a block answer, snapshot build for a full copy), page and segment writes, and the publication's sync groups. CPU time (user + system) is recorded beside wall time. Corpus loading, store preparation and frame encoding are outside the timed span and timed separately.

**The answers.**

1. **Block answers**: delta frames whose records change a uniform random sample of the corpus's definitions, a fraction f of them for f in {0.05, 0.25, 0.5, 1}, each key once; 5% of the sampled keys are tombstones (deletes), the rest updates to a different value of the same shape. The seed is fixed per f.
2. **Full copy**: the snapshot frames of the whole corpus, built into an empty store.

x is an answer's raw encoded record bytes as the agent decodes them, and C the full copy's (|st(s)|, D6's unit), both counted by the harness, not assumed. x / C is therefore measured, not the nominal f.

**The crossing.** With one round trip in both (it cancels), the transfer at link rate L, and the apply times measured above:

```text
T_blocks(x) = x / L + T_apply(x)        T_base = C / L + T_full
α(L) = x* / C, where T_blocks(x*) = T_base
```

1. **Fit.** T_apply(x) = a + b · x by least squares over the 100K corpus's 20 runs (4 sizes, 5 runs each), and again over the 5M round's points. If R² < 0.98 or any size's median lies more than 10% from the line, T_apply is instead the piecewise-linear interpolation of the four medians, extended past the last pair by its slope. Both are reported; the rule picks which one sets α.
2. **Solve** for x* at L = 1 Gb/s and 10 Gb/s, with transfer counted on raw bytes (the design's form). The zstd-3 sizes of each answer's frames are recorded and α is also reported with transfer on those sizes, as a sensitivity row that sets nothing.
3. **Clamp.** If T_blocks stays below T_base up to x = 2C (extrapolating the fit), α = 2, the disk cap's bound (D6 "Cut", rule 2). If T_blocks at the smallest measured x already exceeds T_base, α is the fit's crossing if it is positive, else 1/8.
4. **The policy value.** α for `Limits` is the smallest α(L) over both links and every platform measured, so no block answer at the cut is slower than the copy on any measured link and platform. It is rounded down to a multiple of 1/8 (at least 1/8) and stated as a ratio of integers (α_num / α_den, D6 "Every input is replicated", NR-2).
5. **Recorded, deciding nothing:** relay CPU per answer, measured as the CPU of encoding each answer's frames (records to fragments, with today's zstd-3 framing) in the harness, since no block server exists yet; and server bytes read per answer, from the model's `layout` mode at α for the moderate churn (read / sent there) applied to each measured x.
6. **100K and 5M (D85).** α is computed from each of the five 100K runs (its four block sizes and its full copy give one α; the spread is their range) and from the 5M round (one run per arm, as many arms as its time budget allows). If the 5M α lies inside the 100K range, the 100K median α sets the policy value (rule 4). If it lies outside, the 5M round decides, and the verdict says it has no spread of its own.

### T4(7): `completion_sync_probe`

**What is timed.** `completion_sync_probe DIR FILES ROUNDS 1` from `completion:ab0f551` (`bench/sync_probe.cc`), unchanged, at FILES in {64, 128, 256, 1,024, 4,096} files of 1 KiB, each variant's commit timed (writes excluded), on APFS (reference host, `exclusive`) and on ext4 (the VM, `strict` with `QUIET_VM`). Rounds: 15 at 64-1,024 files, 9 at 4,096. 64 and 128 are added to the design's three sizes because the sync groups the decision compares (below) hold 67 to 176 files. The decision reads the batched variant (`IO::sync_files` then the directory barrier), which is what D11's sync groups issue.

**Device flushes.** On Linux, a companion driver (`namespace-measure-flush.cc`, built on the same probe helpers) runs the batched and the per-file variants alone and reads the block device's completed-flush counter (`/sys/block/<dev>/stat`, field 15) before and after each commit, so flushes per group are counted by the kernel. On macOS no flush counter is readable without root; the count stated is completion's own call sequence for `SyncFiles` (one `F_FULLFSYNC` per group, `completion:ab0f551` `docs/mechanisms/sync-files.md`), labelled as by construction.

**Wall per group** at the sync-group sizes the model gives (D6's `files` table) is the linear interpolation of the batched variant's medians between the two measured sizes around it.

### The period-64 wire run

**What is run.** The design model ([namespace-delivery-blocks-model.py](../design/namespace-delivery-blocks-model.py)) gains a `wire` mode: the `layout` simulation, level-0 merged at answer time, at roll-up periods 16 and 64, for the moderate and hot-heavy churns and the 0.30 C key set (the shapes of the architect's `wire.out`), at α in {0.5, 1, 2} and also at T9's α if it is none of those. Stores are sampled 1, 7, 30 and 182 days behind (positions for the key set) and 2, 8, 16, 32 and 64 seals behind. Per shape and period it prints answer / missing bytes (mean and worst), server bytes read / missing (mean and worst), worst answer / copy above the cut, written per day, and the deepest level-0. It is a simulation of stated shapes; the job proves it ran and printed, not that the shapes are the corpus.

### The roll-up period: 16 or 64

Evaluated at T9's α, with period 16 as the baseline, in this order:

1. **Wire veto.** Period 64 is rejected if, in any shape, its worst answer / missing exceeds 1.25 times period 16's, or its worst answer / copy above the cut exceeds 1.00 where period 16's does not. The period stays 16.
2. **Sync-bound.** Otherwise, period 64 is adopted if, on either platform, period 16's largest sync group (176, 139 and 99 files at Zipf 0.8, 1.0 and 1.3) costs at least 250 ms by the batched variant, a quarter of the one-second seal interval at T8's rate, while period 64's (71, 75 and 67) costs under 250 ms.
3. **Efficiency.** Otherwise, period 64 is adopted only if in every shape its mean answer / missing is at most 1.05 times period 16's and its mean server read / missing at most 1.25 times period 16's: within those bands the 22-25% fewer bytes written (model `files`: 2.03 / 1.85 / 1.52 against 2.69 / 2.34 / 1.78 times the raw changes) outweigh a dead share of 6% against 3%. If either band fails, the period stays 16.

The verdict names the rule that decided it, and the design's period, files, flushes and wire figures are updated or confirmed to match.

## Rulings that changed the runs

Recorded before any data they govern; the thresholds, the fit and the decision rules above are unchanged.

1. **D55 (short measurements).** One strict or exclusive job of at most 10 minutes per row, so T4(7) runs fewer rounds than rule 3's 15 and 9 (the sync-bound rule reads 64-256 files, which keep 15, 15 and 7-15).
2. **Probe pin.** The probe is `completion:560c288`, not `ab0f551`: only it honors the 1 KiB file size (journal ruling 9). The batched path is the same code.
3. **Flush counter.** The kernel counter is `/sys/block/<dev>/stat` field 16 (flush requests completed), not "field 15" (discard ticks), which the first VM run read (journal ruling 10).
4. **D85 (5M runs confirm).** T9 is five runs on the 100K corpus plus one budgeted 5M round, replacing five runs at 5M (Common rule 3, T9 rules 1 and 6).

## Verdicts

**α = 1/8 (α_num = 1, α_den = 8); roll-up period 64.** Each by the rule named below; the numbers the design changes follow at the end. The jobs: T9 build 261112-115033-5cb640, prepare 261112-115039-e2a0e2, five 100K runs 261112-115520-7fe532 (exclusive, `cbf1e0f6`), 5M round 261112-115045-0c8be1 (failed); T4(7) APFS 261110-204532-499625, VM 261112-113801-4be23d; wire 261110-132004-7e9617, 261110-132005-0cd390, 261110-132006-e840b1 (α 0.5, 1, 2) and 261112-132124-1b4fd7, 261112-132129-82a3aa, 261112-132134-41fc3c (α 1/8). Both T9 and T4(7) ran with the amended text above (probe `completion:560c288`, flush counter field 16, D55, D85).

### T9: α

The corpus is the 100K one (`corpus-100k.bin`, SHA-256 `3214561e...a5740`), C = 4,998,389 raw bytes (557,555 zstd-3 framed). Medians of five runs (range in brackets), apply wall to `Published`:

| Answer | x raw (x / C) | Apply wall | Apply CPU | decode / plan / publish | Relay encode CPU |
|---|---|---|---|---|---|
| Full copy | 4.998 MB (1) | 0.340 s [0.335-0.354] | 0.271 s | 0.06 / 0.21 / 0.07 s | 0.032 s |
| f 0.05 | 0.240 MB (0.048) | 0.361 s [0.345-0.383] | 0.262 s | 0.00 / 0.28 / 0.07 s | 0.003 s |
| f 0.25 | 1.178 MB (0.236) | 0.627 s [0.607-0.645] | 0.532 s | 0.02 / 0.54 / 0.08 s | 0.011 s |
| f 0.5 | 2.351 MB (0.470) | 0.903 s [0.860-0.981] | 0.804 s | 0.04 / 0.79 / 0.08 s | 0.020 s |
| f 1 | 4.695 MB (0.939) | 1.228 s [1.176-1.308] | 1.133 s | 0.07 / 1.09 / 0.08 s | 0.036 s |

A block answer costs a fixed 0.36-0.38 s, 0.28 s of it in the delta plan at 5% of the keys, against the copy's whole 0.34 s: at 0.048 C the two walls differ by 0.021 s, less than the delta arm's own spread (0.038 s); from 0.25 C on the block answer is slower by 0.29 to 0.89 s. Delta plan costs about 10 µs per record against the snapshot build's 2 µs.

**Fit and crossing** (rule 1-3), per 100K run and pooled (20 runs):

| Run | T_apply = a + b x | R² | Worst median off line | Fit used | α at 1 Gb/s | α at 10 Gb/s |
|---|---|---|---|---|---|---|
| 1 | 0.377 s + 0.916 s/C | 0.9865 | 9.9% | linear | 0.002 | 0.125 (clamp, rule 3) |
| 2 | 0.369 s + 0.950 s/C | 0.9803 | 14.8% | piecewise | 0.060 | 0.036 |
| 3 | 0.397 s + 0.884 s/C | 0.9498 | 21.6% | piecewise | 0.056 | 0.034 |
| 4 | 0.392 s + 0.958 s/C | 0.9551 | 20.4% | piecewise | 0.066 | 0.044 |
| 5 | 0.364 s + 1.066 s/C | 0.9587 | 20.4% | piecewise | 0.077 | 0.055 |
| pooled | 0.380 s + 0.955 s/C | 0.9582 | 17.9% | piecewise | 0.059 | 0.036 |

(`docs/perf/namespace-measure-t9-fit.py`, rule's raw-byte transfer. Per run, the smaller of the two links is 0.002, 0.036, 0.034, 0.044 and 0.055: median 0.036, range 0.002-0.055. The zstd-3 sensitivity row gives 0.033-0.055 and sets nothing. The script extends the piecewise line below the smallest size; rule 3's literal reading, the linear fit's crossing when positive, gives 0.001-0.024 at 1 Gb/s (1/8 on run 3, whose linear crossing is negative) and 1/8 at 10 Gb/s, where it is negative on every run. Either reading puts α under 1/8.) The sixth full copy, which `prepare` prints (0.34 s), is not counted.

**5M round (D85).** It produced a full arm only: C = 266,337,255 raw bytes (25,731,543 framed), apply 14.50 s (CPU 14.02 s, plan 10.5 s, decode 3.1 s, publish 0.66 s, peak RSS 2.8 GB), relay encode CPU 1.83 s; 53.3 times the bytes for 50 times the keys and 43 times the wall. Arm 0.5 then died with `invalid warm paged graph` (`src/detent_local/paged_warm_validator.cc:13`) and the job ended before arms 1, 0.25 and 0.05. There is no 5M α, so there is no disagreement with the 100K result and, by rule 6, the 100K median sets the policy value. The 5M α has no value and no spread. Whether the delta arm's fixed cost grows with the corpus (the warm validator walks the old and the new graph) is open; the copy scaled linearly.

**Policy value (rule 4).** The smallest α(L) over both links is at most 0.055 on every run, below the rule's floor, so α = the floor, **1/8**. The rule's promise that no block answer at the cut is slower than the copy does not hold at the floor: at x = C/8 the fit puts a block answer 0.11-0.14 s slower than the copy at 1 Gb/s and 0.14-0.17 s at 10 Gb/s (33-50% of the copy's 0.34 s), at 100K. The rule did not foresee a crossing under 1/8; lowering the floor is the lead's call (the model's written bytes grow as α falls, see the period section).

**Recorded, deciding nothing (rule 5).** Relay CPU to encode a frame set is 0.003 s (f 0.05), 0.011, 0.020 and 0.036 s (f 1), and 0.032 s for the copy at 100K; 1.83 s for the copy at 5M. Server bytes read per answer, from the model's moderate churn at α = 1/8 (read / sent = 2.153 / 1.243 = 1.73 at period 16, 2.213 / 1.216 = 1.82 at period 64, ratio of the means): about 0.42, 2.04, 4.07 and 8.13 MB for the four sizes at period 16 (0.44, 2.14, 4.28 and 8.55 MB at 64). The sync of one 100K publication is 13-15 files, 3 flushes in 2 groups.

### T4(7): sync-bound (period rule 2)

Probe `completion:560c288`, 1 KiB files, batched variant `IO::sync_files`, medians in ms (range), rounds as D55 allowed:

| Files | APFS (exclusive, 261110-204532-499625) | ext4 VM (strict, 261112-113801-4be23d) | VM flushes, batched / per-file |
|---|---|---|---|
| 64 | 26.0 (23.8-48.0), 15 rounds | 13.2 (11.4-16.2), 15 | 64 / 64 |
| 128 | 29.6 (26.7-49.4), 9 | 24.3 (22.1-25.5), 15 | 128 / 128 |
| 256 | 33.3 (32.6-53.5), 7 | 45.9 (40.9-51.6), 15 | 256 / 256 |
| 1,024 | 65.0 (64.6-65.0), 2 | 185.9 (178.6-190.0), 3 | 1,024 / 1,024 |
| 4,096 | not run (budget: `skip 4096:1`) | 688.3, 1 | 4,096 / 4,096 |

For contrast, the per-file variant is 1,385 ms at 64 files on APFS (53 times the batched) and 87 ms in the VM (6.6 times). Flushes: the VM's field-16 counter reads one device flush per file for both variants (the first VM run read field 15 and showed zeros, ruling 10); macOS issues one `F_FULLFSYNC` per group, by construction (`completion:ab0f551` `docs/mechanisms/sync-files.md`, not counted). This confirms D11: on Linux the barrier saves syncs, not flushes.

Batched wall at the model's sync groups, interpolated between the measured sizes: period 16's 176, 139 and 99 files cost 31.0, 29.9 and 28.0 ms on APFS and 32.4, 26.2 and 19.3 ms in the VM; period 64's 71, 75 and 67 files cost 26.4, 26.6 and 26.2 ms (APFS) and 14.4, 15.1 and 13.7 ms (VM). Period 16's largest group is under 250 ms on both platforms by a factor of 8 (the worst single round at 256 files is 53.5 ms): **rule 2 does not fire.** The VM's flush costs about 0.18 ms per file; period 16 would reach 250 ms at a Linux flush cost of 1.42 ms per file, and period 64 would stay under it up to about 3.3 ms, so real ext4 hardware with dearer flushes could still fire rule 2; no such host was measured (no AWS).

### The roll-up period (rules 1-3, at α = 1/8)

`wire` at α = 1/8, period 64 against period 16 (the ratios; mean answer / missing, worst answer / missing, mean server read / missing, worst answer / copy above the cut, bytes written):

| Shape | sent mean | sent worst | read mean | worst sent / copy | written | level-0 at most |
|---|---|---|---|---|---|---|
| moderate | 1.216 / 1.243 = 0.979 | 1.593 / 1.652 = 0.964 | 2.213 / 2.153 = 1.028 | 0.063 against 0.060 | 2.300 / 2.246 C a day = 1.024 | 28 sections, 0.0427 C (against 16, 0.0248 C) |
| hot-heavy | 1.443 / 1.397 = 1.033 | 2.018 / 2.237 = 0.902 | 2.640 / 2.753 = 0.959 | 0.051 against 0.049 | 3.969 / 4.374 C a day = 0.907 | 23 sections, 0.0333 C |
| keyset 0.30 C | 1.067 / 1.071 = 0.996 | 1.220 / 1.227 = 0.994 | 1.250 / 1.459 = 0.857 | 0.144 against 0.145 | 7.737 / 10.139 C per 10,000 positions = 0.763 | 20 sections, 0.0320 C |

1. **Wire veto (rule 1): does not fire.** The worst answer / missing ratio is at most 0.994 times period 16's (limit 1.25), and no worst answer / copy passes 1.00 (largest 0.144). Also none at α 0.5, 1 or 2 (worst ratio at most 1.027; worst answer / copy 0.617 against 0.172 at α 0.5 hot-heavy, under 1.00).
2. **Sync-bound (rule 2): does not fire** (above).
3. **Efficiency (rule 3): holds.** Mean answer / missing is at most 1.033 times period 16's (limit 1.05) and mean server read / missing at most 1.028 (limit 1.25) in every shape. **Period 64 is adopted by rule 3.**

The rule did not foresee that its stated benefit shrinks at α = 1/8: the 22-25% fewer bytes written holds only for the key set (23.7% fewer), hot-heavy writes 9.3% fewer and the moderate churn 2.4% more. The adoption stands on the bands, which are the rule; the lead may keep 16 if the written-bytes premise is what counts. Both periods write much more at α = 1/8 than at α = 1 (period 16: moderate 2.246 against 0.684 C a day, hot-heavy 4.374 against 3.591, key set 10.139 against 1.561 C per 10,000 positions), since the cost rule cuts far more often at 1/8: 330, 133 and 579 cuts in the sampled span against 22, 7 and 26 at α = 1.

### The design's numbers to change (`docs/design/namespace-delivery.md`, outside this quest's files)

1. **α:** 1 becomes 1/8 (α_num 1, α_den 8) in D6 "Cut" (the sentence "α is 1 until T9 sets it"), Q2, the T9 row, S5.2's `Limits` and the typical and ceiling bounds (about 1.25 α · |st(s)| becomes about 0.16 |st(s)|, 2α · |st(s)| becomes 0.25 |st(s)| plus the newest block).
2. **Roll-up period:** 16 becomes 64, and the alternative "A file per output at one seal every 16" and "Roll-up period 64, staggered" swap roles; level-0 at answer time holds up to 28 sections (0.043 C), not 16.
3. **Model tables at α = 1** (D6 "Bounds" table, its cut-reach, copy-below-the-cut and disk-peak columns, "Server cost per answer": 1.3 to 3.5 times missing at α = 1, 1.5 to 2.8 at α = 1/8 for the shapes run, written 0.68 C a day, 3.6 C, 1.1 to 2.1 C per 10,000 positions): rerun `layout` at α = 1/8 (the mode takes no α argument yet). The wire figures at α = 1/8 are above; moderate churn writes 2.25 C a day, about 600 MB a day at 5M (C = 266 MB), against the design's 180 MB.
4. **T4(7):** the probe is `completion:560c288` at 64-4,096 files, not `ab0f551` at 256-4,096; record 26 / 13 ms (64 files), 31 / 32 ms (176 files) for APFS / ext4; flushes: 1 per group on macOS (by construction), 1 per file on ext4 for both the batched and the per-file variants (D11 confirmed).
5. **T9 row:** five runs at 100K plus one 5M round (D85), not five at 5M and Linux; α = 1/8, set by the floor, with the apply time's fixed cost of 0.37 s (delta plan) as the lever that would raise it.
6. **Open, not the design's:** arm 0.5 at 5M dies in the warm validator (`invalid warm paged graph`) at `1e8d3285` (its `src/` the one merged from main at `4d457cb`); the 100K run never reaches it. A 5M delta apply is a library defect to open as a quest; D85 forbids a new 5M job here.
