# First reads below 1 ms (quest first-read-1ms)

Where a fresh process's first read through the reader core spends its time, what this quest
removed, and what is left. All numbers come from an Intel Core i9-9980HK running macOS 15 (Darwin
24.6), which has no SHA-NI, so SHA-256 runs the sha256 library's portable kernel. The host ran
loaded (load 30-90, a 68% CPU speed limit) during this work: absolute times are about twice an
exclusive hold's, and the shares are the finding.

## Profile

`first-read-gate.sh profile-prepare OUT` builds `first_read_core` with wall-clock marks that
`first-read-probe.py` inserts into the core, attach and page table; `first-read-gate.sh profile
OUT stopped running` runs each cell as the gate does and prints each step's median and p90. Job
`261124-134303-81190d` (strict, main's reader code), medians in us:

| Step | 1 namespace | 1,000 namespaces |
|---|---|---|
| Total | 1,361 | 3,082 |
| Directory trust walk (12 components) | 567 | 534 |
| `selected` read, SHA-256 and decode | 96 | 1,837 |
| Attach: selection, lease, image, `.read` | 236 | 330 |
| First-touch pages (SHA-256 and node checks) | 141 | 11 |
| Segment open and map | 103 | 110 |
| `change` open and map | 76 | 75 |

Warm micro costs on the same host (strict): a directory `openat` for reading 9.7 us, for search
(`O_SEARCH`) 1.6 us; `fstatat` 4.8, `fstat` 1.2, an ACL read 3.1, a file `openat` and `close`
18.7; SHA-256 of the 141 KB 1,000-entry table 1,154 us (CommonCrypto 643-780 us).

## Changes

Each was screened with the gate's `measure` against main (strict beside light holds, six pairs of
200 reads, publisher stopped), as the median paired difference with its noise floor:

| Change | 1 namespace | 1,000 namespaces |
|---|---|---|
| The trust walk opens directories for search on macOS (`Detent:5d879068`) | -243 us (71) | -214 us (121) |
| With duplicate slots found by sorting (`Detent:e7dc35d6`) and one image read per attach (held) | -317 us (96) | -527 us (162) |

Held for test updates outside this quest's files: the trust walk opens each component before
any `fstatat` (screened -333 and -348 us with the first change), and attach takes the image bytes
the selection read.

## Warm lookups

`benchmarks/reader_bench_ab.sh` runs `reader_bench` (100K knobs, int, core) interleaved against
main. Two changes, together, median p50 ns (strict beside light holds, 6 pairs):

| Case | Threads | main | head |
|---|---|---|---|
| hot | 1 | 737.5 | 592.5 |
| hot | 8 | 797.5 | 651.8 |
| scattered | 1 | 1,198 | 989 |
| scattered | 8 | 1,268.8 | 1,068 |

1. `Detent:182bb900` inlines `read_integer`, `decode_node` and `NodeView::slot` (9.5% and 8% of a
   hot lookup by `sample`, each an out-of-line call).
2. `Detent:d20bbadb` checks a matching leaf's key kind on the looked-up key in place, instead of
   zero-filling and copying a 202-byte key at every leaf.

## What is left

The 1,000-namespace cells hash the whole `selected` table on every first read: 60% of the cell on
this host, more than 1 ms by itself. The table's format fixes that digest, so the remedy is a
format or library decision (quest journal, Blocked on).
