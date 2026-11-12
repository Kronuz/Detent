> Status: DRAFT (2026-11-10) Pre-registered decision rules for the three measurements namespace-delivery D6 owes before S5.2: T9 (α), T4(7) (`completion_sync_probe`) and the period-64 wire run. Verdicts follow once the jobs run. Maintainer: Detent campaign, quest namespace-measure.

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

Pending: no measurement has run.
