# first-read-1ms: journal

State: working
Quest: .campaign/detent/quests/first-read-1ms.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Change 1 (trust walk opens directories with O_SEARCH on macOS) screened against main:
`.scratch/fr/ab.sh LABEL stopped` (strict, the gate's `measure` over the local build, main's
`first_read_core` from `.scratch/fr/out/main`). Next changes are listed under Next.

## Next

1. A1 job 261124-134303-81190d (strict, probe profile, four cells): record it.
2. Change 2: trust walk without the pre/post `fstatat` per component (classify on ENOTDIR only).
3. Change 3: `decode_table` without the `std::set` and per-entry overheads (~310 us for 1,000 entries).
4. Change 4: one image read per attach (`select_entry` and `attach` both open and read `.image`).
5. reader_bench hot path (1 and 8 threads, hot and scattered) against main.
6. A2 exclusive gate runs (stopped, running) at the final head; A3 via `campaign verify` and CI.

## Blocked on

1. The lead: the 1,000-namespace cells cannot reach 1 ms on this host while every first read
   hashes the whole `selected` table with SHA-256 (141 KB for 1,000 entries; 1,154 us on this
   i9-9980HK, which has no SHA-NI, so the sha256 library runs its portable kernel). The table's
   format (D43, namespace-s7) fixes the digest. Options, for the lead or owner:
   a. A faster table checksum or a chunked digest (a format change; greenfield, no compat).
   b. An AVX2 SHA-256 kernel in the sha256 library for x86 without SHA-NI (about 2x, not enough).
   c. Accept the 1,000-namespace cells above 1 ms on hosts without SHA hardware.
   Recommendation: (a), a chunked digest, so a first read hashes its entry's chunk only.
2. The lead: the running 1,000-namespace cell cannot run on main. The test writer
   (`src/detent_testing/generation_writer.cc`, outside this quest's files) limits a store to
   1,000 generations, so a 1,000-namespace store refuses the publisher's next publish ("local
   disk admission pressure"). Raising that limit is one line; the probe skips the cell meanwhile.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pending job 261124-134303-81190d; local profile below |

## Profile (local, before the A1 job)

Probe marks (`docs/perf/first-read-probe.py`), 1,000 namespaces, stopped, strict beside light
holds at `Detent:d599a61d`, 100 reads: median 3,659 us, p99 4,859 us. Top costs (medians):
`selected` read 1,987 us (SHA-256 of 141 KB 1,154 us, decode 310 us), trust walk 754 us (12
components, each fstatat, openat, fstat, ACL, fstatat, close), attach 420 us (select_entry 116,
lease 110, image 72, metadata 102), segment open and map 150 us, `change` open 98 us.
Micro (strict): directory openat O_RDONLY 9.7 us, O_SEARCH 1.6 us; fstatat 4.8, fstat 1.2, ACL
3.1, file openat+close 18.7. The host ran at a 68% CPU speed limit under load 30-90, so the
absolute numbers are inflated; the shares are the finding.

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- R1: the gate runs the reader core in both trees once a tree has `src/detent_reader` (reader-s6
  landed): this quest's A/B is core against core; main before reader-s6 still runs its Client.
