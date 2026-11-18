# reader-s6: journal

State: waiting on jobs: every job in the home `.scratch/reader-s6-moved.txt` not kept, plus plain c435d5 d779ed 701d0a and A7 5046f5, cancelled and rerun at the commit that records this State (A14 fix); probe of `test_reader_switch capacity` there; msg-1 before 318fd2 (`Detent:b1d281c`) and after 63e21d (head). Ids: `campaign status reader-s6`.
Quest: .campaign/detent/quests/reader-s6.md (the lead's; never edit it)

## Doing
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

1. A16: put dab009's and f1bca2's cell lines in `first-read-gate.md` Results and Evidence. Read the smoke a01c45 first; if it fails, fix `first_read_phases.cc` or the gate script (head
   moves: rerun gates). When dab009 and f1bca2 pass, put the per-cell numbers in Evidence.
2. Fill Evidence as the other jobs pass; msg-1 from 0f6566 and f07232.
3. Before landing, regenerate every mutant at the final head (`mutant.py` now chains same-path edits).

## Blocked on

- Nothing. Evidence head: the lead moved every job to `Detent:64d8e34`; the A14 fix moves it
  once more, and every job is rerun at this commit.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A3, A5, A6, A15 | native 1790af (queued, `Detent:bd6581b`) |
| A4 | t3-asan 1c93af, t3-tsan 300581 (queued); asan 04640c, tsan 631262 |
| A7 | 1bf0d7 (queued) |
| A8 | mutants killed: D5-1 9b4bb9, D5-2 4e137b, D5-3 f5658e, D10-1 855a2e, D10-2 347eee, D10-3 4baa90, D2-1 34bd94, D4-1 363cb7, FIX-1 7ae4a1, FIX-2 d7fb02 (at `Detent:4fafed3`); Q6-1 90e1ea queued |
| A9 | [artifact] `docs/mechanisms/reader-core.md`, sections "Fork (D10)" and "Fork and cold preparation" |
| A10 | mutants killed 862642 cc96ed 9d6c47 ea240d 4d9871; plain c435d5 queued (76dd6c passed at `Detent:8ccef6c`) |
| A11 | mutant killed ef5614; plain d779ed queued (62b83d passed at `Detent:8ccef6c`) |
| A12 | F1 c09e87 killed; F2-F4 fc5e6e 2934a2 ba2018, cold cases 701d0a, soak cc66a6 4e9d80 2fcb28 ec46f4 ca8025 b84d11 e3734b f6d5ec queued |
| A13 | 701d0a queued (`test_reader_switch cold`, child ledger check) |
| A14 | 0206b9 failed (writer disk limit, test fix in this commit); rerun pending |
| A16 | dab009 (stopped), f1bca2 (running), exclusive after smoke a01c45; numbers pending |

## Messages

- msg-1: open until before 318fd2 and after 63e21d finish; then the answer is ns per lookup, before and
  after, at 1 and 8 threads, for hot and scattered keys (100K knobs is fine for msg-1, per the lead).
- msg-2: applied: `minimum_page_bytes` for the pack extent, header compare before any bitmap
  answer, `layout_version` gone with main's lease, `state_commitment` replaces `root.logical`,
  DES-7 merged (`verified_page_bytes`, `Detent:4fafed3`).
- msg-79 (mine to the lead): closed by the lead; `generation.cc` and `paged_navigation.hh` added.

## Result

Pending.

## Rulings I made

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
