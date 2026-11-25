# First-read gate (reader.md T2, D49, D121, D140, D142)

Checks two bounds on fresh-process first reads through the reader core: a pooled p99 below 1 ms
(quest first-read-1ms, A2), and no slower than main's beyond the noise floor, at the median and at
p99 (D140). A cell fails on either. 200 of 200 below 1 ms is reported, not gated. Each cell (1 or 1,000 namespaces, publisher stopped or running) interleaves the
branch's core reads with main's own first reads. Script: `docs/perf/first-read-gate.sh`. Both
programs are built from `tests/detent_system/first_read_phases.cc`.

## What it times

Each sample is its own process, timed with `steady_clock` from the first step to the checked
first value. Process launch and dynamic loading are excluded.

| Side | Program | Steps timed |
|---|---|---|
| Branch (reader core) | `first_read_core --core DIR NAME EXPECTED` | `detent_open`, then the first `detent_get` |
| Main (pre-S6 Client) | `first_read_phases --client DIR NAMESPACE KEY EXPECTED` | the Client's retirement runtime, `PreparedReader::open`, its refresh thread, the first `try_get` |

A tree with `src/detent_reader` (main since reader-s6) runs the reader core in both arms; only a
tree before it runs the Client.

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

`first-read-gate.sh measure OUT [STATE[:NAMESPACES]...]` runs under `exclusive` (D142). Each cell runs six
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

The gate has two rules per cell, and a cell fails on either (the cell's `pass`; `below_1ms` and
`within_main` give each rule's answer):

1. A2's bound: the p99 of the cell's 1,200 pooled branch reads is below 1 ms.
2. D140, where main ran: the median of the six paired differences (branch minus main) is at most
   the noise floor, and the pooled branch p99 is at most main's pooled p99 plus p99's floor: the
   spread of main's six run p99s in the cell, maximum minus minimum (D142), since a tail statistic
   needs a tail floor.

The one cell main cannot run (running, 1,000 namespaces: main's test writer cannot publish that
store) has the bound alone; main refusing any other cell fails it. The job's last line counts the
cells that pass both. One function, `cell_pass`, gives every cell its `pass`, and
`first-read-gate.sh selftest` checks it on fixed numbers: a cell below 1 ms but slower than main
fails, so does one within main at 1 ms or more, and so does a cell main refused that main runs. A main read that exits nonzero is counted in its run's `failed` and its
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

`first-read-gate.sh profile-prepare OUT` and `profile OUT stopped running` time each step of the
core's first read with marks that `first-read-probe.py` inserts; `first-read-1ms.md` has the
profile and what it found.


`first_read_phases --phases` splits the Client's read further: it adds the directory trust walk
cold and warm ahead of attach and reports segment mapping time. `--micro DIR ITERATIONS` prints
warm per-operation means: the attach's syscalls, the ACL checks, a pack mapping, SHA-256 of 4 KiB,
thread start, the trust walk and attach. Run both by hand against a prepared store.

## Results

Exclusive, six interleaved pairs of 200 reads per cell, branch `Detent:28a8d276` (first-read-1ms fix
round 2) against main `Detent:34fe15ad`; every cell passes both rules where main ran:

| Cell | Job | Branch pooled p99 | Main pooled p99 | Paired median (floor) | Below 1 ms, branch / main (of 1,200) |
|---|---|---|---|---|---|
| stopped, 1 | 261124-221058-b76dd6 | 764 us | 846 us | -87 us (36) | 1,199 / 1,197 |
| stopped, 1,000 | 261124-221101-ac8c51 | 752 us | 2,114 us | -847 us (48) | 1,195 / 0 |
| running, 1 | 261124-221103-8b1704 | 859 us | 1,168 us | -124 us (16) | 1,194 / 1,161 |
| running, 1,000 | 261124-221106-893160 | 713 us | publisher refused | n/a | 1,199 / none ran |
