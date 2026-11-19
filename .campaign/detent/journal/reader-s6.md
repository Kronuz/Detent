# reader-s6: journal

State: waiting on jobs 261118-184532-ea0609 (profile, light) and 261118-184707-b9981e (A/B, strict). msg-1 open: about 130 ns per hot lookup remains at `Detent:18adf2b` (bench aa9860).
Quest: .campaign/detent/quests/reader-s6.md (the lead's; never edit it)

## Doing
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
  - Not mine to fix here, for the lead: tsan 3c4a1b and t3-tsan ea7399 fail on a TSan data race
    in `test_reader_switch`; A16 b69551 and da956e fail (185-198 of 200 below 1 ms; main's arm
    also exits 1 in da956e: "invalid local generation descriptor").
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

1. Read the probe, HDR mutants and bench; put the bench table in Result and close msg-1.
2. A16: when 797791 and 537ae0 report, put their cell lines in `first-read-gate.md` Results and
   Evidence; if they still fail (branch runs at 198-199 of 200), profile the branch's open phase.
3. Mark Evidence as the jobs pass. Mutants are regenerated at `Detent:18adf2b`; a code move needs
   them again.

## Blocked on

(none)

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

- msg-1: attribution in Result; fix at `Detent:73c0b6c`, after-bench aa9860 at `Detent:18adf2b`; closes with its table.
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
