# legacy-h review: correctness

Commit: Detent:a9eb4f77d8685ef585d7f13f25317763eef593fb (`quest/detent/legacy-h`; its successor
Detent:62e0e0a9 changes only the journal). Fix round 1 over the previous review at Detent:e30799b8:
`git diff e30799b8 a9eb4f77` less main's merge. Detent:dec0d978 has the same code (a9eb4f77 adds one
line to `docs/mechanisms/reader-core.md`).
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The round fixes what it set out to fix: the core reads the agent's one-entry-per-namespace table,
`detent get` and the bindings read a multi-namespace store by value, the Docs gate is green on its
merits, and `hold_retry` is testing-only. Two new defects sit in the per-namespace core under
concurrent publish. A table read between the agent's `selected` rename and its `change` growth fails
with `DETENT_SOURCE_FAILURE` (open, refresh and first lookups), which my stress probe hit 414,516
times in 150 s of namespace adds (F1). And a refresh is all-or-nothing across a handle's namespaces:
one attached namespace whose next generation cannot attach freezes every other namespace on that
handle at a stale value that answers OK (F2).

## Old findings

| Old | Fixed? | Evidence |
|---|---|---|
| F1 (`detent get` fails on a multi-entry store) | Yes | Detent:407e4410 reads the whole table and attaches the looked-up namespace's entry. Probe 261123-204301-46672f at Detent:dec0d978: the website's pages on an agent store, 0 of 15 reads differ from main's `get`. `test_reader_scenarios` runs `detent get` on every integer row over a five-entry store (native 261123-215328-7c1230, 218 of 218). Fails without: 261123-175727-3d4b80, 261123-200448-f006cc, 261123-204320-f48488 (exit 8 at the one-entry core) |
| F2 (no API reads a multi-entry store) | Yes | The bindings' scenario table now reads five entries through C++, Python and Rust (A2-A4: 261123-215328-7c1230, 261123-204301-a7a0cc); my `lists` probe (below) checks listed and unlisted handles by value |
| F3 (Docs gate red) | Yes, not silenced | Docs 45328287755 at Detent:dec0d978: `check-docs`, `e2e`, `site` green. `check_docs.py` takes the statuses from `reader.h` (the `ErrorCategory` names it dropped are printed nowhere: `category_name` has no caller outside `src/detent/errors.cc`). Four stale exceptions are removed; the one added, `guides/cpp-application#1`, carries its reason, as F3 asked; `shell-and-agent#6`'s is gone. Mutant M3 retargeted to `generation_dir` (old F11) |

## Evidence I relied on

- Native 261123-215328-7c1230 at Detent:a9eb4f77: full configure (authority, relay, examples,
  benchmarks, Python, Rust), 218 of 218.
- CI 45328288209 at Detent:dec0d978, every job green: `linux-tsan` and `linux-asan-ubsan` run
  `test_reader_core`, `test_reader_switch`, `test_reader_scenarios`, `test_reader_syscalls` and
  `test_reader_files` (job logs 138223181631, 138223181878; 214 and 216 of 216 pass). So the
  reader-core tests are covered under TSan and ASan at this code; I ran no sanitizer locally.
- T2 261123-204301-53e01c (strict, Detent:dec0d978): hot lookup p50 918-969 ns at 1 namespace,
  935-1051 ns at 1,000; scattered (every namespace attached) 1471-1519 ns and 1271-1288 ns; 0
  allocations. Per-lookup cost does not grow with the namespace count beyond the binary search
  (`View::find`, `core.cc:151-155`); a lookup allocates nothing (Q3).
- The lead's fails-without jobs and the e2e probe above.

## What I checked myself

Probe job 261124-001332-65c668 (strict, 10 min, at Detent:a9eb4f77, slot
`detent-review-legacy-h-correctness`); scripts in `.scratch/detent/review-legacy-h-correctness/` in
the home: `build.sh` (Release, tests on, `review_probe` added through `probe.cmake` without touching
tracked files), `run.sh`, `review_probe.cc`. Rerun it on a fix: it exits 0 only when every RESULT
passes.

- `lists` (Q2): PASS. A handle opened with `{a, c}` reads `a`, refuses `b` and `d` with
  `NOT_OPENED`, answers `c` UNAVAILABLE until `c` appears and then its value; a handle with no list
  reads `c` and `d` once added.
- `window` (Q1, F1): FAIL. At the agent store's `rename table` primitive
  (`AgentStoreTesting` hook) of a publish that adds `b`: `detent_open` returns `source_failure`,
  `detent_refresh` returns `source_failure`, a first lookup of `b` returns `source_failure`. After
  the publish completes, both read correctly.
- `couple` (Q1, F2): FAIL. `u`'s next generation cannot attach (its `.read` hidden, as the suite's
  own failed-switch cases do); `t` then publishes 3 and 4. A handle opened with `{t}` reads 3, then
  4; a handle that attached `t` and `u` reads `t:k` = 1 both times, status OK, and its
  `detent_refresh` returns `unavailable`. Restoring the file and one more publish recovers both.
- `readd` (F3): FAIL as a reader-side check; see F3 for why the agent cannot reach it today.
- `stress` (Q1): 150 s, a writer thread (983 steps: per-namespace deltas, 65 full publishes that
  each drop one namespace and add a new one, so `change` grows past every mapping 65 times, 24
  collections) against 6 reader threads on a handle of all namespaces and 2 on a handle of five,
  plus a refresher every 7 ms. 1.14 billion lookups: no value went backwards, none came from the
  future, no namespace answered UNAVAILABLE after answering OK, the listed handle refused every
  other namespace with `NOT_OPENED`, at most four views lived, and every member converged to its
  last value on both handles. But 414,516 lookups and 358 of 35,250 refreshes returned
  `source_failure` (status 4, a `Failure` thrown, consistent with F1's path); the invariant RESULT
  fails on those alone.
- Q4: `nm` on the shipped `libdetent_reader.a` and `libdetent_reader.dylib` finds no `hold_retry`
  or `reader::testing` symbol; only `libdetent_reader_hooked.a` has it. It is defined inside
  `#ifdef DETENT_READER_TEST_HOOKS` (`core.cc:816-823`), the exports list holds the seven `detent_*`
  functions, and `test_reader_exports` passes in native and CI. It only sets `retry_at`, which the
  next successful publish resets.
- Read: the table read against reader.md D4. Rule 1 holds: `read_table` (`core.cc:402-433`) loads
  the store word and every sequence it covers before reading `selected`, and a first attach at an
  unchanged word reuses the view's table and its pre-read sequences. `selected` is read whole,
  digest-checked, with sorted names and unique slots enforced by `decode_table`
  (`store_table.cc:96-142`), so a torn or rewritten table is refused or re-read, never searched
  half-written; views and their `Table` are immutable once published. Sequences are 64-bit and
  compared for equality: no wrap. A slot reused for another namespace (the agent recomputes the next
  slot from the current entries, `agent_store.cc:437-458`, so a dropped highest slot can return)
  only costs a dropped namespace extra refreshes. Rule 4's remap runs only in the refresh winner.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A table read while the agent adds a namespace fails with `DETENT_SOURCE_FAILURE`. The agent renames `selected` (naming the new slot), syncs the directory, and only then grows `change` and bumps it (`src/detent_local/agent_store.cc:466-485`). A read in that window finds a slot past the file: `read_table` remaps, still finds it uncovered, and after four attempts throws `Failure{DETENT_SOURCE_FAILURE}` (`src/detent_reader/core.cc:418-431`). So `detent_open` (hence `detent get`, Python's `Reader()`, Rust's `Reader::open`) fails, `detent_refresh` fails, and the failure is remembered at the store word, so first lookups of unattached namespaces return it until the word moves. Reader.md D4 rule 4 wants `change` to cover every slot the table names; the landed agent breaks that for one fsync. The single-entry core never read a newly named slot, so this round exposes it | Probe 261124-001332-65c668 `window` (open, refresh and first lookup all `source_failure` inside the window) and `stress` (414,516 lookups and 358 refreshes `source_failure` over 65 adds) | Either or both, the lead's routing: (a) the agent grows `change` (`reserve`, `resize_change`) before it writes and renames `selected`; zero words ahead of the table are harmless. (b) The reader treats a slot past the remapped file as a table ahead of the store word: answer from the installed view or UNAVAILABLE and retry when the word moves, never SOURCE_FAILURE. Test: `test_reader_core` opens, refreshes and looks up from the agent store's `rename table` hook during a publish that adds a namespace, all OK or UNAVAILABLE |
| F2 | yes | defect | A refresh attaches every attached namespace whose generation changed and publishes all of them or none (`core.cc:503-538`, the catch at `:554-581`). When one namespace's next generation fails to attach (a damaged or untrusted generation, an A4 regression, or `DETENT_NO_MEMORY` at the handle's shared mapping cap), the whole refresh fails, the failure is remembered at the store word, and every other namespace on that handle keeps answering its old generation with status OK, through each later publish, for as long as that one stays broken. A handle that never attached the broken namespace follows normally. Reader.md D4 says a failed attach "remembers the sequence it failed at", and A21 asks that per-namespace sequences decide refresh; main's per-namespace sources were independent | Probe 261124-001332-65c668 `couple`: `t:k` stays 1 on the handle that attached `u` through two `t` publishes (3, 4) while a `{t}` handle reads 3 and 4; `detent_refresh` returns `unavailable` | Make attach failures per entry: a namespace whose attach fails keeps its previous generation and its old `seen` (so it retries when its own sequence moves or on `detent_refresh`), the others publish. Test: the `couple` case in `test_reader_core`, `t` reading 3 and 4 on the handle that attached `u` |
| F3 | no | nit | `attach` keeps the previous generation when the table names the same generation id (`core.cc:503-505`) but `add` then watches the previous generation's slot (`:521-525`), not the entry's. A namespace dropped and re-added at the same cursor (same derived id, `derived_ids.cc:41-53`; a new slot, `agent_store.cc:455-458`) would watch its old slot and miss its later publishes. The agent cannot reach this today: while a reader holds the dropped generation's lease, the re-adding cycle is rejected with `LeaseHeld` (`agent_store.cc:351-366`) | Probe 261124-001332-65c668 `readd` (hand-written tables): `a:k` stays 1 after `a`'s next publish while a new handle reads 5 | Keep `previous` only when the slot matches too |
| F4 | no | gap | A namespace that leaves the table is retained for the handle's life (`core.cc:528-532`, reader-core.md "Build"). Its lease then makes the agent reject a cycle that re-adds it at a revisited cursor (`LeaseHeld`, `disk_store.cc:24-33`, rethrown by `coordinator.cc:577-580`) until the handle closes, and nothing releases it. Main's client did the same (`client_source.cc:132-147` keeps the installed view when a refresh throws, at Detent:18ce371d), so this is pre-existing, not this round's | Code as cited (not reproduced: the test writer cannot revisit a cursor) | A follow-up: release a dropped namespace's generation at a later refresh, or say in reader-core.md what holds it |

## Not run

- Sanitizers locally: CI's `linux-tsan` and `linux-asan-ubsan` at Detent:dec0d978 run every reader
  test; my probes ran in a Release build on macOS only, without a sanitizer.
- F4's agent stall end to end, and F3 through the real agent (both need a revisited cursor, which
  the test writer cannot produce).
- Linux and the bindings beyond what native 261123-215328-7c1230 and CI 45328288209 ran.

This review ran as a subagent of the lead's session, not as a session titled
`detent · reviewer legacy-h correctness`.
