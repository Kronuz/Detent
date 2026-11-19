# reader-s6: journal

State: blocked on the lead (Blocked on 1, ruling 1 of 2026-11-19 needs two files outside the set and rests on a premise the code does not hold); also waiting on jobs 261119-044036-f68cf2 (running), 261119-044037-fee608 261119-044037-891946 261119-044037-22df97 261119-044037-b4fe42 261119-044038-baa905 261119-044041-2d8fe1 (queued) at `Detent:2fb2a41`. Pushed.
Risk questions for the reviewers:
- Security: (1) `Detent:09be4f1` places a page's nodes end from its raw external count: can a page changed after first touch (a Linux private mapping shows file writes until copy-on-write) steer any `decode_node`/`decode_external` read outside the page? (2) SR1: does any open of `change`, `selected`, the image, lease or pack files still block on a FIFO or follow a planted symlink? (3) SR2: can a replacement between `mmap` and the identity `fstat` bind a mapping to the wrong file? (4) Fork: can a child see a half-prepared page or inherit a ledger entry for a mapping it lacks? (5) Is `testing::stats` compiled only with `DETENT_READER_TEST_HOOKS`?
- Correctness: (1) Ring and one freer: can a view be freed while a pin holds it (D5's seq_cst reload; STATS-1 58fade shows the hook now pins)? (2) Is a page's verified bit published only after `verify_page` and the first-touch walk complete, and read with acquire? (3) Q6 (a): is the empty-store reopen at most once per 200 ms under 16 concurrent readers? (4) Does the merge of main's format refusal (`Detent:c9eac84`) leave the reader refusing old formats the same way as main? (5) A16's split: does `count` plus a short exclusive `measure` still test D49's bound?
Quest: .campaign/detent/quests/reader-s6.md (the lead's; never edit it)

## Doing
- Results recorded 2026-11-19:
  - msg-1 bench dd2b26 (exclusive, `Detent:518085f`, no header decode in lookup), 1 thread median
    p50: core hot 467.5 (reps 467 463 485 468), scattered 711.5 (reps 719 702 704 729); aa9860 at
    `Detent:18adf2b` 526 and 805; before 393 and 659. The decode is 59 ns hot and 94 ns scattered.
  - Profile baceeb (light, `Detent:b1d281c` against `Detent:18adf2b`, hot 1 thread), self time as ns
    per call (share times p50), core: `decode_node` +42 (0 before: the old lookup walked nodes
    inline), `decode_page_header` +28, `validate` (header and reference) +22, `verified_page_bytes`
    hit compare +27, `lookup` self +25, `decode_external` +9 and `slot` +6, `decode_definition_header`
    +7; offsets: `decode_reference` -22, `PageTable::State::resolve` -16 and `find` -11. Pinned
    matches within 3 ns. So the +133 ns is: header decode and its validate about 50-59, the shared
    out-of-line node codec (`decode_node`, `slot`, more `lookup` self) about 70, and the hit compare
    with reference validation net about +20 against the old resolve.
  - `Detent:09be4f1` (raw nodes end) broke native 132c68: `test_dense_reads` mutation 3 sets a
    verified page's node-count high byte (`root[14] = 0xff`) after first touch and requires the next
    lookup to refuse it; only `validate(PageHeader)` does. Reverted at `Detent:fa32973`.
  - A16 under the old gate at `Detent:518085f`: count 842f21 passed (no branch failure; main refused
    1 read in 2 cells, light, so its timings are not evidence). Breakdown 0992ec (exclusive,
    running): 1 namespace +34 us against a 56 us floor; 1,000 namespaces +72.6 us against a 53 us
    floor (branch medians 720-742 us, main 614-721 us), so it would fail D121 too.
  - t3-tsan 279a47 (`Detent:518085f`) failed the third repeat at `test_reader_switch.cc:649`: the
    cold-fork child exited nonzero with no message of its own (T3 itself held 4 of 4). t3-asan
    9c507c and tsan b41c94 passed. `Detent:2fb2a41` prints the child's status, exit and signal.
  - D121 (owner): `Detent:74a0913` gates each cell on the median paired difference at most the noise
    floor and the p99 of the cell's 1,200 pooled branch reads below 1 ms; 200/200 is printed only.
- Results recorded 2026-11-18 (resumed session):
  - Line-275 attribution, proven: the STATS-1 run symbolized (473f09) ends with descriptors 6 of 4,
    the two extra both `/dev/ptmx`, which is the pseudo-terminal TSan's macOS symbolizer runs `atos`
    on to symbolize the race reports. The same build with `symbolize=0` (fe4089) ends 4 of 4; head
    under TSan (ab35b3, no race, so no symbolizer) 4 of 4; head native (f94397) 3 of 3. No leak.
  - TSan race: `Detent:69ea114` introduced it (A14's `change_bytes` stat reads `view->change`
    unpinned). TSan at `Detent:b1d281c` 01ef48 exit 0; at `Detent:32e65d8` a39a36 exit 8 with the
    race at `core.cc:724` in `testing::stats`; only `Detent:69ea114` touches `change_bytes` between.
    Fixed at `Detent:c471f2f`; STATS-1 58fade killed (exit 8, race at `core.cc:725`).
  - msg-1 A/B b9981e (strict, 6 interleaved pairs, beside 7 light holds), HDR-2 (raw external count,
    no header decode) against `Detent:18adf2b`, median p50 ns: core hot 759 to 658.5 (-100.5),
    pinned hot 768 to 649 (-119), core scattered 1300 to 1075.5 (-224.5), pinned scattered 1315.5
    to 1075 (-240.5). The decode is 13-15% of a hot lookup and 17-18% of a scattered one. Scaled to
    aa9860's exclusive numbers (core hot 1 thread 526, scattered 805; before 393 and 659), removing
    it predicts about 455 hot and 665 scattered: scattered recovered, hot still about 60 ns (16%)
    over. `Detent:09be4f1` makes HDR-2 the code; dd2b26 runs aa9860's command at the head.
  - Profile ea0609 failed because `prof.sh` returned the status of `wait` on the bench it killed
    (143), not a missing path: the first sample (`before-core`) was written. `prof.sh` now checks the
    sample file instead; resubmitted as baceeb. It attributes the remaining hot gap at `Detent:18adf2b`.
  - A16 stopped 369849 (exclusive, `Detent:751b648`) failed both cells, and main misses too: branch
    below 1 ms per run 200 195 200 198 200 199 and 200 200 200 200 199 198; main 199 200 199 199 199
    200 and 198 200 200 200 199 200; main failed 0. Paired median difference +27 us (1 namespace) and +45 us
    (1,000 namespaces), under the noise floors 52 and 53 us. Every slow read, either arm, slows in every
    phase at once with faults unchanged (branch open 300 to 590-875 us and lookup 240 to 410-570 us,
    user and system both; main attach 340 to 630-1,180 us): the process was slowed, not a code path.
  - Lead (owner-approved): A16 split. `Detent:518085f` adds `first-read-gate.sh count` (light: fails
    only on a branch failure, totals main's refusals): 842f21. The exclusive `measure ... running`
    keeps the timing and slow-read breakdown (10 min): 0992ec. Submitting 0992ec superseded e69c8b.
- Lead reorder (2026-11-18): TSan race, then A16, then msg-1.
- TSan (3c4a1b, ea7399; both warnings are the same pair). Read of 8 bytes by T18, the test's
  stats thread (`switching`, `test_reader_switch.cc:695`), in `testing::stats` (`core.cc:724`:
  `view->change` through a view loaded relaxed from the ring, unpinned). Previous write by a
  lookup thread (T5, T13) in `refresh_locked` (`core.cc:402`: `built->change = change`), under
  `detent_get` (`core.cc:654`), in the 48-byte `View` that thread allocated at `:401`. The reader
  introduced it: `git log -S change_bytes` names only `Detent:69ea114` (A14's stats field). It is
  a test-hook read (`DETENT_READER_TEST_HOOKS`), but unpinned it could also read a freed view.
  Jobs 01ef48 (`Detent:b1d281c`) and a39a36 (`Detent:32e65d8`) run the same TSan test to confirm
  it. Fix `Detent:c471f2f`: `stats` pins the view as a lookup does (`Pin`, `acquire`). Mutant
  STATS-1 (the unpinned relaxed read back; `.scratch/mutants-tsan.json`) is 63fdce, run
  `--fails-without`. TSan's "invariant at line 275" (T3's threads, descriptors and store mappings
  back to baseline after close) failed in the same runs. It is unexplained until a run shows the
  cause; the lead will not accept it as tooling noise without one. `Detent:751b648`: T3 prints the
  counts at baseline and at the end, and names each descriptor open at the end but not at baseline
  (path or type). `.scratch/t3/t3.sh` runs `test_reader_switch switch` and passes when the invariant
  holds: STATS-1 symbolized 473f09, STATS-1 `TSAN_OPTIONS=symbolize=0` fe4089, head TSan ab35b3,
  head without TSan f94397. If symbolize=0 holds where symbolized fails, it is the symbolizer's
  pipe. Otherwise the named descriptor is a leak, and I fix it.
- A16: each arm already reads a store its own code wrote. `prepare` generates `main/store-*` with
  main's `first_read_phases` and `branch/store-*` with the branch's, and each publisher is its own
  arm's binary, so there is no format mismatch. Main's arm exits on "invalid local generation
  descriptor": main's `check_generation_file` (`generation_file_access.hh:17-23` at
  `Detent:8cd932c`) requires `st_nlink == 1`, and `read_generation_record` refuses a short read, so
  a generation file the running publisher collects under main's Client is refused. The branch
  reads those again (`Detent:47470ea`, `checked_or_unlinked`). `Detent:ebaa638`: the gate counts
  main's failed reads per run instead of stopping, and prints `phase_medians` (wall, CPU, faults)
  and every read at or over 1 ms (`slow`), to attribute the branch's misses. Reruns c7f32f
  (stopped) and 28641a (running). In b69551 and da956e the branch's medians were 551-761 us and
  main's 531-670 us; open was 306-393 us and the first lookup 240-377 us.
- msg-1, second round (lead, 2026-11-18): (1) profile core hot t1 at `Detent:18adf2b` and
  `Detent:b1d281c`, ns per frame; (2) why the scratch A/B overstated the gain; (3) fix what is
  avoidable on the hit path, rerun aa9860's exclusive command; inherent checks go to Blocked on.
  - Profile 261118-184532-ea0609 (light, `sample` 8 s of hot t1, core and pinned, both arms; the
    bench jobs' own binaries and stores, copied to `.scratch/prof2/`, run by `prof2/run.sh`).
  - A/B 261118-184707-b9981e (strict, 6 interleaved pairs, one store): HDR-2 (lookup reads the
    externals raw, no header decode) against `Detent:18adf2b`, `.scratch/ab2/`. It prices the
    header decode the fix kept.
  - (2), from the code: the scratch patch (`.scratch/exp/src`) replaced the header decode inside
    `decode_node` and `decode_external` with one `read_integer` and decoded no header at all; the
    real fix still runs one full `decode_page_header` per reached page in `lookup` (magic, zero
    bytes, four `read_integer`, a 32-byte digest copy, `validate(PageHeader)`). The A/B also ran
    strict on shared cores (pair 1 core hot: 711 head, 530 patched). b9981e measures the gap.
- `Detent:3e6b09c`: `test_dense_reads.cc:282` constructs `LazyPageAccess` without the factory
  (lead granted it). Build-all 261118-144659-de07eb: 528 of 528 targets built.
- `Detent:18adf2b`: `touched` flips the mapped byte through a writable private copy of its page
  (`mprotect`); a macOS private mapping does not show a file write after first touch (probe
  837cb3 failed on that precondition). Build-all and `touched` 261118-145947-360816 passed.
- `Detent:612f2d0` (lead granted the file): `test_foreign_generation.cc` constructs
  `LazyPageAccess` without the factory; this broke every check build at `Detent:32e65d8`.
- msg-1 fix, `Detent:73c0b6c` (lead granted the four codec files): `decode_node` and
  `decode_external` take the nodes end of a header the caller decoded (`paged::nodes_end(h)`);
  the `Limits` overloads stay as wrappers for the walkers outside the file set. `lookup` decodes
  each reached page's header once; `verify_page_structure` and the first-touch walk pass the end
  they hold. Guard test: `test_lazy_page_bitmap touched` damages a first-touched page through
  the file, checks the mapping shows it, and requires the next `definition()` to be refused, at
  the logical digest (hit compare) and at the magic (lookup's header decode). Mutants HDR-1 (hit
  compare gone) adf29d and HDR-2 (lookup takes the externals raw, no header decode) 7b363f,
  `.scratch/mutants-hdr.json`.
- A14 failure (0206b9 at `Detent:bd6581b`): the cause is the test, not the reader. `capacity`
  grows `change` to 1 GiB, then publishes 40 revisions with the same `GenerationWriter`. The
  agent's census counts `change` at its logical size (`agent_store.cc` `resize_change`), and the
  writer's `DiskLimits.bytes` is 1024 segments = 1 GiB, so the next publication's
  `DiskLedger::reserve` throws `DiskPressure`. The reader reserves nothing in that ledger; the
  three sizes passed. Fix: the ledger stress runs on its own store (`capacity-ledgers`).
- `Detent:bd6581b` ports the first-read gate (lead ruling on A16): `first_read_core --core` times a
  fresh process's `detent_open` and first `detent_get`; main's arm is `first_read_phases --client`
  (runtime, `PreparedReader::open`, refresh thread, first `try_get`), built from main's tree.
  `--generate` builds each tree's own store (10,000 int knobs `n<i % N>:k<i>` = 3i + 1, N = 1 and
  1,000) inside the job under `.scratch/first-read`; `--publish` is the running publisher (a
  one-knob delta to `n0` every 20 ms, collected every 16). `measure` runs six interleaved
  main/branch pairs per cell, 200 reads per run, and fails unless every branch run is 200/200
  below 1 ms; it prints each arm's medians, p99s, the paired difference and the noise floor.
- Jobs at `Detent:8ccef6c`: A10 76dd6c and A11 62b83d passed; the rest were cancelled when the
  head moved and resubmitted at `Detent:bd6581b`. A7 2f0400 failed in 1 s: the slot had no
  `build-reader` (it was reset), and the command did not configure; 1bf0d7 configures first.
- Q6-1 62fc42 survived because of a harness bug, not a code gap: `mutant.py` read each edit from
  the base, so with two edits to `core.cc` only the last one landed. The `remembered` gate was
  removed but `refresh_locked`'s 200 ms check was left in place. `mutant.py` now chains the edits
  to one path, and Q6-1 at `daff3f2` removes both (90e1ea). No other mutant edits one path twice.


- `Detent:470a127` merged main; `Detent:4fafed3` fixes SR1 and FIX-2 in `read_selected`
  (`generation.cc`, granted by the lead): non-blocking opens of `selected`, its image and the
  store's other record opens; an unlinked table or image returns nullopt and is read again
  (`checked_or_unlinked`, one fstat, now in `generation_file_access.hh` and shared with
  `prepared_reader.cc`). `test_reader_files` adds `.image` (35 cases). The record-open hook
  (`RecordTesting`) moved with it and fires for `selected`, so `replaced_selected` is FIX-2's
  deterministic regression again.
- DES-7 done: `paged_navigation.hh` `verified_page_bytes` is the one page check (hit compare;
  decode and compare; commitment; caller's first-touch checks; bit). `checked_page_bytes` and
  `LazyPageAccess::resolve` both call it.
- Probe 261118-094346-d77f12 builds the reader targets and runs core, files, syscalls, lazy-page,
  change-file and switch parks/forks/cold.
- Mutants (`.scratch/mutants.json`, `.scratch/mutant.py` make `mutant/reader-s6/<name>` commits on
  `Detent:4fafed3`), each `--fails-without`, after the probe:
  D5-1 9b4bb9, D5-2 4e137b, D5-3 f5658e, D10-1 855a2e, D10-2 347eee, D10-3 4baa90, D2-1 34bd94,
  D4-1 363cb7, Q6-1 62fc42, FIX-1 7ae4a1, FIX-2 d7fb02 (A8); SR1 selected 862642, image cc96ed,
  change 9d6c47, record ea240d, pack 4d9871 (A10); SR2 ef5614 (A11); freeze F1 998758 (no recheck
  after the count), F2 eb96b2 (no drain), F3 17a24b (child keeps counts), F4 82cbad (prepare
  freezes no table) (A12). All ids are `261118-0945xx-<suffix>`.

- `Detent:69ea114`: A14 stress as `test_reader_switch capacity` (`change` at 1 MiB, 64 MiB,
  1 GiB maps whole, answers in < 2 s, mappings and descriptors back to baseline; heap and mapped
  ledgers after 40 publishes equal their values after 10; values printed). A13: the cold-fork child
  now requires each inherited handle's mapped ledger to equal a fresh handle's for the same read.
  Stats gain `heap_bytes`, `mapped_bytes`, `change_bytes` (hooked build only).
- A7 job 2f0400 (head, after the probe). msg-1: `reader_bench` exclusive, 200K knobs, int hot and
  scattered, core and pinned, 1 and 8 threads, 4 reps x 1M calls: before 0f6566 at
  `Detent:b1d281c`, after f07232 at `Detent:69ea114`. The exclusive cap is 10 min, not 25.

## Next

1. msg-1: on the lead's answer to Blocked on 1, implement (a) or (b), run `touched`, `test_dense_reads`
   and aa9860's command, and write the final table to `docs/perf/`.
2. A16: read baa905 and 2d8fe1 (D121).
2a. t3-tsan: read b4fe42's child status.
3. Gates 132c68 65b1c1 b41c94 9c507c 279a47; then rerun mutants (HDR-1, STATS-1, A8, A10-A12),
   soak and A7-A14 at the settled head, and mark Evidence.

## Blocked on

1. msg-1, lead ruling 1 (2026-11-19: mutation 3 stays; validate at first touch, keep a private copy,
   lookups never read the mapped value). What the code shows:
   - Lookup never reads the node count. `PagedReadState::lookup` uses the header only for its nodes
     end (length minus 68 x externals, offset 12); the node count (offset 14) is read only by
     `validate(PageHeader)` and `verify_page_structure`. With `Detent:09be4f1`, mutation 3's damaged
     count was ignored and the read returned the verified result; the test failed because it demands
     a refusal. Under the ruled assertion (verified result or refusal) 09be4f1 passes mutation 3 as is.
   - The value that does steer is the external count: it places every external entry. The reverted
     code reads it from the mapping on every lookup too (`decode_page_header` bounds it, so a damaged
     count that stays in bounds steers both before and after 09be4f1).
   - The body also steers: slots, labels, prefixes and external references are read from the mapping
     on every lookup, and a Linux private mapping shows file writes until copy-on-write. "A value
     damaged after verification never steers a read" holds for no mapped byte today.
   Options:
   (a) Private external count (what the ruling needs for the header): `VerifiedPages` keeps a 16-bit
       count per 80-byte granule, stored before the bit's release and read after its acquire (2 bytes
       per 80 mapped, 2.5% of mapped bytes as ledgered heap, 5x the bitmap; A14's ledger values move).
       `PagedPageAccess` gains a resolve that returns bytes and the verified nodes end (default: decode
       the header). Needs `src/detent_local/verified_pages.hh` and `src/detent_local/paged_page_access.hh`
       (outside the set; 12 other `PagedPageAccess`/`VerifiedPages` users compile against them).
       Mutation 3 asserts verified-or-refused; a new case damages the external count within bounds and
       asserts the verified result; the mutant reading the mapped count must fail it. Expected: the
       59 ns (dd2b26).
   (b) Reapply 09be4f1 with mutation 3 asserting verified-or-refused, and state that header and body
       bytes after first touch are trusted as the mapping's owner wrote them (the reader already
       requires the publisher's uid). Same 59 ns, no memory.
   (c) Private page copies at first touch (heap equal to touched bytes) for the full guarantee.
   Recommend (a) with the two files granted, and the body question (b vs c) to the owner via the
   security review.
2. A16 under D121: 0992ec (running, 1,000 namespaces) is +72.6 us against a 53 us floor at
   `Detent:518085f`; baa905 and 2d8fe1 decide it at the head. If it fails, I profile the branch's
   open and lookup phases there.
3. t3-tsan cold-fork child failure (279a47, 1 of 3 repeats): a real failure until named; b4fe42 at
   `Detent:2fb2a41` prints the child's status. If it is the product, I fix it.
4. Remaining hot gap after msg-1 (lead ruling 2): the shared node codec, about 70 ns of 133
   (baceeb), goes to paged-decode-one-shape; it is stated here and goes into `docs/perf/` with the
   final bench. Scattered must not regress (659 before; 805 at `Detent:18adf2b`, 711 without the
   header decode).

## Evidence

All at `Detent:18adf2b`; mutants regenerated there (`.scratch/mutant.py`). Older passes no longer count.
Probe 485215 (codec, dense, lazy-page with `touched`, reader core and files) passed.

| Criterion | Evidence |
|---|---|
| A1-A3, A5, A6, A15 | native 8858e9 |
| A4 | asan 1c6cee, tsan 3c4a1b, t3-asan eb1d87, t3-tsan ea7399 |
| A7 | b9837d |
| A8 | mutants D5-1 8445c8, D5-2 1fe887, D5-3 6a8a03, D10-1 092144, D10-2 431265, D10-3 2d19f4, D2-1 b4eafd, D4-1 3c206c, FIX-1 bfde28, FIX-2 3f55ca, Q6-1 3f52f6, HDR-1 adf29d, HDR-2 7b363f (D5-1 to D4-1 killed) |
| A9 | [artifact] `docs/mechanisms/reader-core.md`, sections "Fork (D10)" and "Fork and cold preparation" |
| A10 | plain 2332d5; mutants SR1-selected e182b0, SR1-image 4bef61, SR1-change b73f2b, SR1-record e176e3, SR1-pack 736a52 |
| A11 | plain 269fec; mutant SR2 c9e640 |
| A12 | cold 161706; mutants F1 3a55c5, F2 822725, F3 533ecf, F4 2e752f; soak chained (D120) 03744a a32223 0f00a7 6d3b7c d70d82 824720 57d00a c47ec5 |
| A13 | 161706 |
| A14 | 9d28de |
| A16 | b69551 (stopped), da956e (running); at `Detent:32e65d8` 0d023e and 1fc66f failed (198-200 of 200 per run, branch medians 30-60 us above main) |

## Messages

- msg-1: attributed (Doing, baceeb and dd2b26); fix `Detent:73c0b6c` (one header decode per page);
  `Detent:09be4f1` reverted; open on Blocked on 1.
- msg-2: applied: `minimum_page_bytes` for the pack extent, header compare before any bitmap
  answer, `layout_version` gone with main's lease, `state_commitment` replaces `root.logical`,
  DES-7 merged (`verified_page_bytes`, `Detent:4fafed3`).
- msg-79 (mine to the lead): closed by the lead; `generation.cc` and `paged_navigation.hh` added.

## Result

msg-1, `reader_bench`, 100K knobs (`--generate 50000`), int, 4 reps x 1M calls, exclusive. Median
of per-thread p50 / p99 in ns; before 318fd2 at `Detent:b1d281c`, after 63e21d at `Detent:32e65d8`:

| Case | 1 thread before | 1 thread after | 8 threads before | 8 threads after |
|---|---|---|---|---|
| core hot | 393 / 446 | 575 / 728 | 468 / 1052 | 702 / 1601 |
| pinned hot | 394 / 430 | 578 / 724 | 495 / 1086 | 724 / 1645 |
| core scattered | 659 / 1092 | 980 / 1475 | 798 / 1695 | 1140 / 2590 |
| pinned scattered | 687 / 1130 | 897 / 1214 | 831 / 1714 | 1124 / 2640 |

Each call includes the clock (about 65 ns). The stores differ in their rollout knobs (bool before, int
rollout values after, per D1), not in the int keys timed.

Attribution (msg-1 regression):
- Profile: `sample` of hot 1-thread, both arms, both commits (`.scratch/prof/*.sample.txt`). After,
  `decode_page_header` is 9.6% of the thread's samples and `decode_node` 8.5%, with
  `decode_external`, `decode_child` and `get_signed` 3.4% more; before had none of them on the hit
  path. Clock share fell from 16.9% to 12.3%: per call is 1.37x longer.
- Experiment: head plus a scratch patch that reads the external count instead of decoding the
  header in `decode_node`/`decode_external`, interleaved with head, 4 pairs, strict hold, 1 thread,
  same store (`.scratch/exp/ab.txt`). Median p50, head to patched: pinned hot 580 to 440
  (-140), pinned scattered 939 to 700 (-239), core hot 591 to 530 (-62, noisy pair), core scattered
  943 to 692 (-251). That recovers most of the hot gap (394 before) and all of the scattered one.
- Cleared: `checked_or_unlinked`'s fstat is not on the lookup path (no syscall but the clock in
  any sample); D1 `detent_get` dispatch adds nothing measurable (core and pinned move together);
  `verified_page_bytes` self time fell (446 samples in `resolve` before, 228 after).

## Rulings I made

- Lead rulings 2026-11-19 recorded: mutation 3 stays; node-step inlining goes to
  paged-decode-one-shape and the quest may land with that gap stated; t3-asan keeps 16 readers
  (the lead handles CPU accounting); the t3-tsan child exit is a real failure until named.

- msg-1 raw nodes end (`Detent:09be4f1`) reverted at `Detent:fa32973`: `test_dense_reads`
  mutation 3 is dense's contract, so dropping the header refusal is the lead's call (Blocked on 1).
  HDR-2 stays a mutant.
- A16 D121 by value: "p99" is the cell's pooled branch reads (1,200), and "within the noise floor"
  is a median paired difference at most the floor, so a faster branch passes.
- Deferred until msg-1 settles: the mutants, soak and A7-A14 reruns at the head, since a further
  msg-1 change moves the head again. Submitted now only the gates, the bench, the profile and A16.

- Job moves for the TSan and A16 step: at `Detent:751b648` (ebaa638's jobs cancelled) only build-all, STATS-1, both A16 cells and
  (after build-all) the gate checks. The mutants, soak and A7-A14 move once, after the msg-1 fix, since
  that moves the head again. A16 keeps BASELINE `Detent:8cd932c` so its numbers compare with b69551
  and da956e.

- msg-1 fix shape: the nodes end, not the header, is what `decode_node`/`decode_external` take;
  `decode_external` derives the external count from it and requires an exact fit, so a wrong end
  cannot reach outside the page. The `Limits` overloads stay (seven walkers outside the file set
  call them). Removing them is the lead's next quest, paged-decode-one-shape.
- HDR-1 and HDR-2 count toward A8 (a dying mutant per claim): the claim is that a page damaged
  after first touch is still refused.

- msg-1: f07232 aborted because `--generate 100000` writes 200K knobs and `parse_defaults` caps a
  defaults file at `kDefaultMaxKnobs` = 100,000. That holds at both `Detent:b1d281c` and the head,
  so the before arm (0f6566) would fail too, and a bench fix at the head can't reach it. Both arms
  now run `--generate 50000` (100K knobs, the cap): before 318fd2, after 63e21d, exclusive, 10 min.
  The head is unchanged.

- A14: the ledger stress publishes into a fresh store instead of raising the writer's disk limit
  (`src/detent_testing`, outside the file set). Running soak rounds at `Detent:64d8e34` were
  cancelled too: each runs the full `test_reader_switch`, `capacity` included, so each would fail.

- Main removed `StorageIdentity` and `storage.meta`; the core's "store identity change" refusal goes
  with them. A swapped store still fails the A4 regression check within an incarnation and the
  `change` identity check on refresh.
- C ABI follows main's reader.md D1 (one-knob-type): `detent_get(handle, name, hash, switch,
  fallback, out)`; `detent_eval`, `detent_read`, `detent_value`, `DETENT_WRONG_TYPE` removed;
  statuses renumbered to D1's table. C++ `Reader::get`, `get_as<T>` (out-of-range or a non-0/1
  bool returns the default), `lookup(name, fallback, ...)` per D7.
- Stale mutants: only `test_reader_switch cold` changed since `Detent:4fafed3` (A13's child ledger
  check; `parks`, `forks`, `test_reader_core` and `test_reader_files` are unchanged), so F1-F4
  were cancelled (998758 eb96b2 17a24b 82cbad) and resubmitted at `Detent:8ccef6c`; the other 17
  stay at `Detent:4fafed3`.
- A16 gate shape: the gated number is each process's `total_ns` from before `detent_open` (or
  the Client's runtime) to the checked first value, launch and loading excluded as before. A run
  clones the tree's store (`cp -c`), reads its files into the cache, and starts five warmup
  processes. Each tree builds its own store, as the 5M gate did. The gate targets join each tree
  through `CMAKE_PROJECT_detent_INCLUDE` with a deferred `include`, so no `CMakeLists.txt` is
  edited.
- Retiring the 5M gate (lead's ruling): `docs/perf/first-read-gate.md` now describes only the ported
  gate. `tests/detent_system/first_read_gate.py` is deleted, since `git grep` finds no caller. The
  file set names `docs/perf/first_read_gate.py`, which does not exist, so the lead should correct
  the path to `tests/detent_system/first_read_gate.py`. `first_read_export.cc` stays, because
  `docs/perf/one-knob-type.sh` builds it.
- A12 soak: eight jobs of one round each (four concurrent full `test_reader_switch`, Release),
  since 8 rounds took about 2 h on 2026-11-07 and a light soak job is capped at 30 min.
- A verified hit compares the reference's length, keys, height and digest with the page header's
  raw fields (as `checked_page_bytes`), without a full decode.
