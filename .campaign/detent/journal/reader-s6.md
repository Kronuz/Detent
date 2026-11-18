# reader-s6: journal

State: waiting on jobs 261118-094346-d77f12 (probe), 21 mutants (Doing), 261118-094839-2f0400 (A7), 261118-094816-0f6566 and 261118-094806-f07232 (msg-1 bench)
Quest: .campaign/detent/quests/reader-s6.md (the lead's; never edit it)

## Doing

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

1. Probe green: `campaign verify reader-s6` (native, asan, tsan, t3-asan, t3-tsan).
2. Plain-test jobs `--for` A1-A3, A5, A6, A10, A11, A13, A14 (`test_reader_switch capacity`);
   the A12 soak (four copies of `test_reader_switch`, eight rounds).
3. msg-1: read 0f6566 and f07232; answer with core vs pinned p50/p99.
4. A16 first-read gate through the core.
5. Regenerate mutants if the code changes before landing.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A16 | none yet at the merged head |

## Messages

- msg-1: open; T1 numbers are the lead's to measure on the merged head (exclusive `reader_bench`).
- msg-2: applied: `minimum_page_bytes` for the pack extent, header compare before any bitmap
  answer, `layout_version` gone with main's lease, `state_commitment` replaces `root.logical`,
  DES-7 merged (`verified_page_bytes`, `Detent:4fafed3`).
- msg-79 (mine to the lead): closed by the lead; `generation.cc` and `paged_navigation.hh` added.

## Result

Pending.

## Rulings I made

- Main removed `StorageIdentity` and `storage.meta`; the core's "store identity change" refusal goes
  with them. A swapped store still fails the A4 regression check within an incarnation and the
  `change` identity check on refresh.
- C ABI follows main's reader.md D1 (one-knob-type): `detent_get(handle, name, hash, switch,
  fallback, out)`; `detent_eval`, `detent_read`, `detent_value`, `DETENT_WRONG_TYPE` removed;
  statuses renumbered to D1's table. C++ `Reader::get`, `get_as<T>` (out-of-range or a non-0/1
  bool returns the default), `lookup(name, fallback, ...)` per D7.
- A verified hit compares the reference's length, keys, height and digest with the page header's
  raw fields (as `checked_page_bytes`), without a full decode.
