# First-read gate (A15, reader.md T2)

Measures 200 fresh-process first reads per run for the baseline (`main` when Dense merges it) and
the Dense head, over 1- and 1,000-namespace stores of the same 5M corpus, with the publisher
running and stopped, in six interleaved baseline/Dense pairs per cell.

## What it times

The pre-S6 read path: `prepared_client_probe --read` measures `first_live_ns` with
`steady_clock`, from before `detent::Client` construction (managed read source) to the first
checked value; process launch and dynamic loading are excluded. reader.md T2 names S6's
`detent_open`; S6 is not on `main`, so this gate times the path that exists on both sides. Each
measured process follows a warmup process for its key, and every arm reads its store's files into
the page cache first (resident data). 40 integer keys from the 5M recipe, 5 rounds.

## How it is built

`first-read-gate.sh prepare` builds both trees in one light job with Homebrew LLVM 23.1.2
(the qualification's toolchain; the job fails on any other version), Release (`-O3`),
`DETENT_READ_COUNTERS=OFF`, with the SDK that LLVM's config file names as the sysroot (another
SDK puts its C headers ahead of libc++ through zlib's include dir), and checks the compiler path,
its detected version, the sysroot, the build type and the counters in each CMake cache. Each tree exports the
pinned corpus (`DENSE_CORPUS`, sha256 recorded) with its own builder through
`tests/detent_system/first_read_export.cc` (layout version 2 for the baseline, 3 for Dense),
which also writes the empty owner lock and the `.read` and `.live` records the agent's cold start
opens, derived from one full recovery as `tests/detent_local/trusted_records.hh` does. The
1,000-namespace shape is test_dense_fullbuild's:
every record in `primary` plus 999 namespaces of 100 integer knobs (5,099,900 records).

`first-read-gate.sh measure` runs `tests/detent_system/first_read_gate.py` under `exclusive`;
a trailing `stopped` or `running` runs only that publisher state's arms, so each half fits one
30-minute hold.
Each (namespaces, publisher) cell runs six pairs, baseline then Dense, alternating
(B, D, B, D, ...); the first pair's order flips from cell to cell (stopped/1 starts with the
baseline, stopped/1000 with Dense, running/1 with the baseline, running/1000 with Dense). Runs use
fresh APFS clones. A running run starts the tree's own `detent-agent` over the clone with its
upstream port bound but not listening (as the 2026-10-26 qualification did) and measures once the
agent logs readiness. With 12 runs of about 6 s per cell, each publisher state's job (24 runs)
takes a few minutes and fits one 30-minute hold.

`result.json` records per run the count below 1 ms, median, p99, maximum, median attach, lookup
and CPU, major faults, and input hashes, and per cell the D51 verdict:

1. Each arm's six run medians, in run order.
2. The six paired differences (Dense minus baseline, pair by pair).
3. Each arm's spread: the median absolute difference of consecutive same-arm run medians (five
   differences).
4. The noise floor: the larger of the two spreads.
5. Dense is NO WORSE iff the median of the six paired differences is at most the noise floor;
   otherwise WORSE, a real regression.

The runner prints every run median, the paired differences, both spreads, the noise floor, the
counts below 1 ms and the verdict as one `A15 verdict` line per cell. The 1 ms bound belongs to
reader-s6.

## Attribution

`first-read-gate.sh profile PREPARED OUT` (exclusive) splits the first read into the steps the
pre-S6 Client takes, with `tests/detent_system/first_read_phases.cc` built from each tree: the
process-default retirement runtime (its thread), the directory trust walk cold and again warm, the
prepared attach (`PreparedReader::open`), the refresh worker's thread, and the lookup with its
segment mappings, each with wall time, user and system CPU and minor faults. Per arm it also
records warm per-operation means (`--micro`: each syscall the attach makes, the ACL checks, a
pack mapping, SHA-256 of 4 KiB, thread start, the whole trust walk and attach). Arms: B1, D1,
D1000, B1000 at the job's path (11 components), then D1 and B1 cloned to a 4-component path; per
key a warmup, one `--read` sample and one `--phases` sample, each its own process.
