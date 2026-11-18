# reader-s6: journal

State: waiting on jobs: gates 36c38c b0bfa4 22758c 27f6fb 89843a; plain 76dd6c 62b83d cb2a71 55b928; soak 2b8463 8df624 48524e 4e87a1 fb8998 0d1e8b f45dad 538d0c; mutants (17 kept at `Detent:4fafed3`, F1-F4 c09e87 fc5e6e 2934a2 ba2018 at head); A7 2f0400; msg-1 0f6566 f07232. A16 blocked (Blocked on).
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

1. Fill Evidence as the jobs above pass (all ids `261118-09xxxx-<suffix>`).
2. msg-1: read 0f6566 and f07232; answer with core vs pinned p50/p99.
3. A16 once the lead rules on Blocked on.
4. Regenerate every mutant at the final head if the code changes before landing.

## Blocked on

- A16 (first-read gate, D49). `docs/perf/first-read-gate.sh` times the pre-S6 `detent::Client`
  (`prepared_client_probe --read`), not `detent_open`/`detent_get`; its inputs
  (`.scratch/capacity-trials/5000000-restore-r12`: `expected-values.bin`, `agent-0/managed.json`,
  and `DENSE_CORPUS`) are gone from the home; and `first-read-gate.sh`, `first_read_gate.py` and
  `first_read_phases.cc` are outside this file set. Options:
  1. Grant `docs/perf/first-read-gate.*` and `tests/detent_system/first_read_gate.py`; I add a
     core arm (a fresh-process `detent_open` + first `detent_get` probe in `tests/detent_reader/`)
     interleaved against main's Client arm, and the lead restores or names the 5M corpus.
  2. Same probe on a corpus `reader_bench --generate` builds (no 5M restore), 1 and 1,000
     namespaces, publisher stopped and running, exclusive under 10 min per state.
  Recommendation: 2 now (it proves the 1 ms bound through the core with what exists), 1 when the
  corpus is back if the lead wants the main comparison on the 5M shape.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A3, A5, A6, A15 | native 36c38c (queued, head `Detent:8ccef6c`) |
| A4 | t3-asan 27f6fb, t3-tsan 89843a (queued); asan b0bfa4, tsan 22758c |
| A7 | 2f0400 (queued, `Detent:69ea114`) |
| A8 | mutants 9b4bb9 4e137b f5658e 855a2e 347eee 4baa90 34bd94 363cb7 62fc42 7ae4a1 d7fb02 (at `Detent:4fafed3`; their tests unchanged since) |
| A9 | [artifact] `docs/mechanisms/reader-core.md`, sections "Fork (D10)" and "Fork and cold preparation" |
| A10 | plain 76dd6c; mutants 862642 cc96ed 9d6c47 ea240d 4d9871 |
| A11 | plain 62b83d; mutant ef5614 |
| A12 | cold cases cb2a71; soak rounds 2b8463 8df624 48524e 4e87a1 fb8998 0d1e8b f45dad 538d0c; mutants F1-F4 c09e87 fc5e6e 2934a2 ba2018 (head) |
| A13 | cb2a71 (`test_reader_switch cold`, child ledger check) |
| A14 | 55b928 (`test_reader_switch capacity`) |
| A16 | blocked (Blocked on) |

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
- Stale mutants: only `test_reader_switch cold` changed since `Detent:4fafed3` (A13's child ledger
  check; `parks`, `forks`, `test_reader_core` and `test_reader_files` are unchanged), so F1-F4
  were cancelled (998758 eb96b2 17a24b 82cbad) and resubmitted at `Detent:8ccef6c`; the other 17
  stay at `Detent:4fafed3`.
- A12 soak: eight jobs of one round each (four concurrent full `test_reader_switch`, Release),
  since 8 rounds took about 2 h on 2026-11-07 and a light soak job is capped at 30 min.
- A verified hit compares the reference's length, keys, height and digest with the page header's
  raw fields (as `checked_page_bytes`), without a full decode.
