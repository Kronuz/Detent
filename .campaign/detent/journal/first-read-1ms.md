# first-read-1ms: journal

State: working
Quest: .campaign/detent/quests/first-read-1ms.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Hot path: `benchmarks/reader_bench_ab.sh` (interleaved main/head, core, int, hot and scattered, 1 and
8 threads) built locally under `.scratch/fr/rb`; then profile hot 1-thread and fix in
`src/detent_local` if the cost is there.

## Next

1. A1 job 261124-134303-81190d (strict, probe profile, four cells): record it when it runs.
2. Hot path A/B and fix (above).
3. When the lead answers Blocked on 1-3: apply the held patches (changes 2 and 4) with their test
   updates, screen, then queue A2 (exclusive, stopped and running) and `campaign verify --for A3`.

## Blocked on

1. The lead: the 1,000-namespace cells cannot reach 1 ms on this host while every first read
   hashes the whole `selected` table with SHA-256 (141 KB for 1,000 entries; 1,154 us on this
   i9-9980HK, which has no SHA-NI, so the sha256 library runs its portable kernel; 83% of a table
   decode by `sample`). The table's format (D43, namespace-s7) fixes the digest. Options:
   a. A chunked table digest (or a cheaper checksum), so a first read hashes its entry's chunk:
      a format change; greenfield, no compat. Recommended.
   b. An x86 SHA-256 kernel without SHA-NI in the sha256 library (CommonCrypto runs 141 KB in
      643-780 us against the portable kernel's 1,169-1,382 us here, about 1.9x): helps every
      SHA-256 on this host (table, image, `.read`, first-touch pages) but leaves the table at
      about 600 us.
   c. Accept the 1,000-namespace cells above 1 ms on hosts without SHA hardware.
2. The lead: widen the file set by two test files, so two measured changes can land:
   - Change 2 (`.scratch/fr/change2-walk-precheck.patch`): the trust walk opens each component
     first and runs `fstatat` only to classify a failed open (alias or refusal), keeping the
     post-open name check. Screen with change 1: -333 us (1 ns) and -348 us (1,000 ns) against
     main, against change 1 alone -243 and -214 (floors 30-155 us). It needs
     `tests/detent_local/test_trusted_paths.cc`: its race hook fires on the second `fstatat` of
     "raced" (`race_calls == 2`), which becomes the first.
   - Change 4 (`.scratch/fr/change4-image-once.patch`): attach takes the image bytes the
     selection read (with their ACL checked there), so a first read opens `.image` once, not
     twice. It needs `tests/detent_local/test_read_counters.cc`: the Client open counts 4 records,
     not 5, and 320 fewer table bytes (CI's counters job).
3. The lead: the running 1,000-namespace cell cannot run on main. The test writer
   (`src/detent_testing/generation_writer.cc`) limits a store to 1,000 generations, so a
   1,000-namespace store refuses the publisher's next publish ("local disk admission pressure").
   Raising that limit is one line; the probe skips the cell meanwhile, and the gate's `measure
   ... running` fails on it.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-134303-81190d (strict, probe at `Detent:3f0d12cb`, the reader code main has): see "Profile (A1)"; rerun at the final head for the after-profile |

## Profile (A1)

Job 261124-134303-81190d (strict beside light holds, main's reader code at `Detent:3f0d12cb`), 200
reads per cell, median of each marked step (us):
- 1 ns stopped: median 1,361, p99 12,346 (one stall). Trust walk 567 (42%), attach 236 (selection
  58, lease 53, image 51, `.read` 64, root decode 10), first-touch pages 141 (SHA-256 verify 113,
  node checks 28), segment open and map 103, `selected` 96, `change` 76, handle 31.
- 1,000 ns stopped: median 3,082, p99 4,915. `selected` 1,837 (60%: its SHA-256 and decode), trust
  walk 534 (17%), attach 330, segment 110, `change` 75.
- 1 ns running: median 1,369, p99 2,462; the same shares (trust walk 518).
- 1,000 ns running: skipped, the publisher cannot start (Blocked on 3).

## Profile (local, before the A1 job)

Probe marks (`docs/perf/first-read-probe.py`), 1,000 namespaces, stopped, strict beside light
holds at `Detent:d599a61d`, 100 reads: median 3,659 us, p99 4,859 us. Top costs (medians):
`selected` read 1,987 us (SHA-256 of 141 KB 1,154 us, decode 310 us), trust walk 754 us (12
components, each fstatat, openat, fstat, ACL, fstatat, close), attach 420 us (select_entry 116,
lease 110, image 72, metadata 102), segment open and map 150 us, `change` open 98 us.
Micro (strict): directory openat O_RDONLY 9.7 us, O_SEARCH 1.6 us; fstatat 4.8, fstat 1.2, ACL
3.1, file openat+close 18.7. The host ran at a 68% CPU speed limit under load 30-90, so the
absolute numbers are inflated; the shares are the finding.

## Screens (local, strict beside light holds, `.scratch/fr/ab.sh`, stopped, 6 pairs x 200)

Median of run medians, us, main against branch; paired difference (floor):
- Change 1 (O_SEARCH, `Detent:5d879068`): 1 ns 1,386 / 1,140, -243 (71); 1,000 ns 3,149 / 2,967,
  -214 (121).
- Changes 1+2: 1 ns 1,375 / 1,024, -333 (30); 1,000 ns 3,169 / 2,918, -348 (155). Held (Blocked 2).
- Changes 1+3+4 (3 = `Detent:e7dc35d6`): 1 ns 1,491 / 1,200, -317 (96); 1,000 ns 3,384 / 2,879,
  -527 (162). Change 4 held (Blocked 2).
The host ran loaded (load 30-90, CPU speed limit 68%): main's 1 ns median is 1.39-1.49 ms here
against about 0.6 ms on an exclusive hold, so these are screens, not A2.

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- R2: change 2 keeps the post-open name check (`test_trusted_paths` "raced" pins it); only the
  pre-open `fstatat` goes. Change 3 refuses the same tables; a duplicate slot is now found after
  the whole table decodes, not at its entry (one TableRefused either way, D43).
- R1: the gate runs the reader core in both trees once a tree has `src/detent_reader` (reader-s6
  landed): this quest's A/B is core against core; main before reader-s6 still runs its Client.
