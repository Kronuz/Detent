# First-read gate (A15, reader.md T2)

Measures 200 fresh-process first reads per arm for the baseline (`main` when Dense merges it) and
the Dense head, over 1- and 1,000-namespace stores of the same 5M corpus, with the publisher
running and stopped, plus an A/A repeat of the 1-namespace stopped arm for each code.

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
then prepares the store with its own probe. The 1,000-namespace shape is test_dense_fullbuild's:
every record in `primary` plus 999 namespaces of 100 integer knobs (5,099,900 records).

`first-read-gate.sh measure` runs `tests/detent_system/first_read_gate.py` under `exclusive`;
a trailing `stopped` or `running` runs only that publisher state's arms, so each half fits one
30-minute hold.
Arms run on fresh APFS clones in this order: stopped B1, D1, D1000, B1000; running B1, D1, D1000,
B1000; A/A D1, B1 stopped. A running arm starts the tree's own `detent-agent` over the clone with
its upstream port bound but not listening (as the 2026-10-26 qualification did) and measures once
the agent logs readiness. `result.json` records per arm the count below 1 ms, median, p99,
maximum, median attach, lookup and CPU, major faults, and input hashes.
