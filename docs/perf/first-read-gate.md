# First-read gate (reader.md T2, D49, D121, D140, D142)

Checks the first-read bound (D140): fresh-process first reads through the reader core are no slower
than main's beyond the noise floor, at the median and at p99. A p99 below 1 ms and 200 of 200 below
1 ms are targets (quest first-read-1ms). Each cell (1 or 1,000 namespaces, publisher stopped or running) interleaves the
branch's core reads with main's own first reads. Script: `docs/perf/first-read-gate.sh`. Both
programs are built from `tests/detent_system/first_read_phases.cc`.

## What it times

Each sample is its own process, timed with `steady_clock` from the first step to the checked
first value. Process launch and dynamic loading are excluded.

| Side | Program | Steps timed |
|---|---|---|
| Branch (reader core) | `first_read_core --core DIR NAME EXPECTED` | `detent_open`, then the first `detent_get` |
| Main (pre-S6 Client) | `first_read_phases --client DIR NAMESPACE KEY EXPECTED` | the Client's retirement runtime, `PreparedReader::open`, its refresh thread, the first `try_get` |

Each sample prints one JSON line: each step's wall time, user and system CPU and minor faults,
`total_ns` (the gated number), and the store path's component count. Reads cycle through 40
integer keys.

## The stores

`first-read-gate.sh prepare OUT` builds both trees with the following settings:

1. Homebrew LLVM 23.1.2. The job fails on any other version.
2. Release, with `DETENT_READ_COUNTERS=OFF`.
3. The SDK named by LLVM's config file as the sysroot.

It checks each CMake cache for these settings. The gate's targets join each tree through
`CMAKE_PROJECT_detent_INCLUDE`, which defers an `include` to the end of the top-level
`CMakeLists.txt`, so no tree is edited. Main comes from the job's `--ref BASELINE=main`.

Each tree writes its own store with its own writer (`first_read_phases --generate`):

- 10,000 int knobs `n<i % N>:k<i>` = 3i + 1, plus `n0:tick`.
- N = 1 or 1,000 namespaces.
- The store lives under `OUT` inside the job's checkout, so the path depth is the job's.

## The runs

`first-read-gate.sh measure OUT [stopped|running]` runs under `exclusive` (D142). Each cell runs six
main/branch pairs, alternating. Main goes first in even cells and the branch in odd ones.

Each run does the following:

1. It clones the tree's store with `cp -c`.
2. For a running cell, it starts that tree's publisher over the clone (`first_read_phases
   --publish`), which publishes a one-knob delta to `n0` every 20 ms and collects every 16
   publications.
3. It reads every store file into the page cache, so the data is resident.
4. It starts five warmup processes, then takes 200 samples.

Per run, the script prints the count below 1 ms, the median, the p99, the maximum and each step's
median. Per cell, it prints one `A16 cell` line with these fields:

- Both arms' counts below 1 ms.
- Both arms' run medians and p99s.
- The median of the six paired differences (branch minus main).
- The noise floor: the larger of the two arms' median absolute differences between consecutive
  run medians.
- The p99 floor: main's largest run p99 minus its smallest.

The gate is D140, per cell: the median of the six paired differences (branch minus main) is at
most the noise floor, and the p99 of the cell's 1,200 pooled branch reads is at most main's pooled
p99 plus p99's floor: the spread of main's six run p99s in the cell, maximum minus minimum (D142),
since a tail statistic needs a tail floor. Both are relative to main because on this host main misses 1 ms too: at
p99 in 3 of 4 cells on an exclusive hold (jobs 9f40bf and 5fb83e), and 200 of 200 per run (job
369849). Both 1 ms figures are reported, not gated. A main read that exits nonzero is counted in its run's `failed` and its
message in `failures`, and the run goes on: with the publisher running, main's Client refuses a
generation file collected under it ("invalid local generation descriptor"), which the branch reads
again. A branch read that exits nonzero stops the job.

Each run line also carries `phase_medians` (wall, user and system CPU and minor faults per phase)
and `slow`, every read at or over 1 ms with the same fields, to attribute the misses.

## How to run it

```sh
campaign job submit --quest reader-s6 --for A16 --level exclusive --minutes 10 --ref BASELINE=main \
  --build 'bash docs/perf/first-read-gate.sh prepare .scratch/first-read' \
  -- bash docs/perf/first-read-gate.sh measure .scratch/first-read stopped
```

Run it once with `stopped` and once with `running`. Each publisher state covers two cells of 2,460
processes and fits one 10-minute exclusive hold.

`first-read-gate.sh count OUT running` runs the same cells under `light`: it fails only if a branch
read fails, and its last line totals main's refused reads. Those refusals depend on the publisher's
collection, not on load, so the exclusive run is kept for the timing and the slow-read breakdown.

## Attribution

`first_read_phases --phases` splits the Client's read further: it adds the directory trust walk
cold and warm ahead of attach and reports segment mapping time. `--micro DIR ITERATIONS` prints
warm per-operation means: the attach's syscalls, the ACL checks, a pack mapping, SHA-256 of 4 KiB,
thread start, the trust walk and attach. Run both by hand against a prepared store.

## Results

Pending: jobs dab009 (stopped) and f1bca2 (running).
