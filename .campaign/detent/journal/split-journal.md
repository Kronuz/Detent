# split-journal: journal

State: ready for review (A2 job 261124-161154-977be8 and CI on journal:a1d039a pending)
Quest: .campaign/detent/quests/split-journal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Verdict table below (A1) written before any move. Code worktree: /Users/kronuz/code/Kronuz/journal-wt/detent/split-journal, base fc5c59b (journal). Local build: ~/.scratch/journal-split/build; base `ctest -N` test lines in ~/.scratch/journal-split/base-ctest.txt (106 tests).

Verdicts (files over 1,000 lines; `git ls-files | xargs wc -l`):

| File | Lines | Verdict |
|---|---|---|
| test/journal_test.cc | 2376 | split: four topics in one file: the Journal on a memory IO (append, recovery, corruption), checkpoints and artifacts, Store, and the reopen/park cases on a Host with the heap counter. Seams at the test-function boundaries; shared harness (check, MemoryIO model) to a support header; main and the parameterized sweeps stay with the heap hooks |
| test/host_test.cc | 1424 | split: Host lifecycle, views and handoff (lines 1-922) stay; the idle-group cases (own `namespace idle` model, 12 cases, 923-1402, one design doc) move to host_idle_test.cc; main and its case list stay in host_test.cc, so the CMake case regex is unchanged |
| test/turn_seal_test.cc | 1180 | keep: one mechanism (TurnDriver::seal), 23 cases sharing Consumer, Model and the staging helpers; the `repeat*` family (about 300 lines) uses the same sealer API and helpers, so each half would be a fragment of the other |
| include/journal/turn_log.h | 1196 | split (file set widened by the lead): turn_log_format.h (options and record types, turn_log_detail: the on-disk format, 493 lines); turn_log.h keeps the TurnLog class (750 lines, whole: a restructure, not a move) |
| include/journal/host.h | 1068 | split: host_table.h (HostOptions, GroupEntry, GroupStatus, host_detail: layout and shard codecs, trie, range tables, 311 lines); host.h keeps PrefixIO (37 lines, joins its neighbor) and the Host class (806 lines, whole) |

## Next

1. On the jobs and CI: answer the lead's A3 read. Pushed: quest/detent/split-journal at journal:a1d039a. Keep ~/.scratch/journal-split/tools until job 261124-161154-977be8 has run.

## Blocked on

Nothing. Tool bug for the lead: cpp-libraries tools/split_check.py (bc16496) misreads a hex digit separator (`0xffff'ffffu`, turn_log.h:151) as a character literal, so every line after it counts as inside a function and the turn_log_detail run fails ("moved lines ends inside a function"). A2 runs a scratch copy with the one-line fix (~/.scratch/journal-split/tools/split_check.py: the `'` test also requires a hex digit before and after); the stock tool fails at the base's own text.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/dev/splits.md` (journal:a1d039a): split table (4 files), kept-whole table (turn_seal_test.cc) |
| A2 | job 261124-161154-977be8 (patched copy of split_check.py, `--base fc5c59b492ff include test`); 261124-155553-798354 cancelled (older code); local at a1d039a: 0 errors, 57 warnings |
| A3 | [manual] for the lead: `git diff --color-moved=zebra fc5c59b..HEAD -- test` and the verdict table above |
| A4 | `ci:ci` (run 45516181830 at a1d039a, green on every job) |

## Result

host_test.cc 1424 -> host_test.cc 793 + host_idle_test.cc 515 + host_support.h 189 (journal:ed420b3).
journal_test.cc 2376 -> journal_test.cc 640 + journal_artifact_test.cc 557 + journal_store_test.cc 399 +
journal_reopen_test.cc 637 + journal_support.h 308 (journal:24b3653). turn_seal_test.cc kept whole.
Each new file is a source of the same executable (CMakeLists.txt target_sources); case names, the
CMake case regex over host_test.cc and ctest -N are unchanged. turn_log.h 1196 -> turn_log.h 750 + turn_log_format.h 493; host.h 1068 -> host.h 806 + host_table.h 311 (journal:a1d039a); the headers keep their path and include their piece. Layout note: docs/dev/splits.md. No fnsizes diff: headers only, every piece still in the same translation unit.

## Rulings I made

- Support headers keep the shared helpers in an anonymous namespace (one copy per file), so no
  function gains `inline` and no helper changes linkage; the two check counters (`checks`,
  `failures`) are the only shared state: `extern` in the header, defined by the main file at global
  scope (their line moved out of the anonymous namespace: internal to external linkage).
- Cases that main calls from another file leave the anonymous namespace (the host idle cases were
  already global): prototypes in the support header (the `void name();` warnings of split_check).
- Functions with default arguments called by main (checkpoint_publication_failures,
  store_artifact_completions, reclamation_failures, append_failures, recovery_failures) stay in
  journal_test.cc: a split definition would have to drop its defaults, which is not a move.
- The heap counter (replaced operator new/delete, JOURNAL_TEST_TSAN) moved to
  journal_reopen_test.cc with the only cases that read it; the replacement still applies to the whole
  executable.
- The `Steal` template is in host_support.h; each tag and its explicit instantiation sit in the one
  file that uses it (RangesTag in host_test.cc, OpenedTag in host_idle_test.cc), one instantiation each.
- split_check warnings (57): 50 namespace-scope declarations added (the case prototypes and the
  two `extern` counter declarations) and 7 `namespace {` openings for the helpers (anonymous namespaces in
  new code); no dropped `static`, no added `inline`, no moved dynamic initializer, no dropped include
  or comment.
- turn_seal_test.cc kept whole (reason in the table). docs/dev/splits.md is outside the stated file
  set, written because A1 names it.
