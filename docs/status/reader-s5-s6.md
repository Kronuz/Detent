> Status: CURRENT (2026-11-04) Ledger for reader steps S5 and S6. Workstream: reader-s5-s6. Design: [reader.md](../design/reader.md) "First steps"; [namespace-delivery.md](../design/namespace-delivery.md) D8/D9 for the `change` shape.

# reader-s5-s6

- Branches / worktree: `reader/s5` from `Detent:ff0167d`; `reader/s6` cut from `reader/s5`. Worktree `~/code/Kronuz/Detent/.claude/worktrees/agent-a1fbf8344cbf48ecb`.
- Lead calls in force: Q1-Q6 all (a) (`docs/status/reader-design.md`); R1-R4 verbatim.
- State: S5 landed on main (`Detent:d4dbbad`, then `Detent:d1e9ee6` dropped the S6 note from it); CI run 41817708223 green. S6 on `reader/s6` from `Detent:d1e9ee6`: built, tests green locally, mutant group A done; sanitizers, mutant group B and CI in progress.

## Done criteria (from the brief and the design's S5/S6 rows, T3, T6)

S5:
- [x] `change` file in the agent: store word, per-entry sequences, grow-only, created with the store before any `selected`.
- [x] Startup bump after the barrier, before ready.
- [x] Increments after the rename and barrier of every `selected` replacement.
- [x] Exclusive `owner.lock` while the agent writes the store.
- [x] Readers refuse a `change` the publisher does not own (D4 rule 4).
- [x] Test with values; a dying mutant per claim; ASan/UBSan and TSan builds; how-it-works note.
- [x] CI green on `reader/s5` (run 41817708223, every job that ran; freebsd and headers skipped by plan).

S6:
- [x] D1 C ABI, D2 lazy attach, D4 switch, D5 ring and one freer, D10 fork handlers, C++ header.
- [x] Attach takes the store's directory descriptor.
- [x] Q6 (a): open succeeds on an empty store, lookups return the fallback with `DETENT_UNAVAILABLE`, open retried at most once per 200 ms.
- [ ] T3 (switch under 16 readers; injected parks; refusals counted; resources back after close).
- [x] T6 (thread count unchanged; the three fork cases).
- [x] T1 syscall and allocation tests (macOS locally; Linux seccomp in CI).
- [ ] `reader_bench` builds and runs (the lead measures).
- [ ] Values-based tests, a dying mutant per claim, a revert check per fix, ASan/UBSan and TSan, how-it-works note.
- [ ] CI green on `reader/s6`.

## Done

- `Detent:410cfe9` Add the change file to the agent's store (`src/detent_local/change_file.{hh,cc}`, `disk_store.cc`, `inventory.cc`, `store_core.hh`; test `tests/detent_testing/test_change_file.cc`; note `docs/mechanisms/change-file.md`).

## Proof

S5, `test_local_change_file` (values, not counts):
- Writer shape: `{0,0}` → publish{0} `{1,1}` → reserve(4) `{1,1,0,0,0}` → publish{2,3} `{2,1,0,1,1}` → publish{} `{3,1,0,1,1}` → bump_all `{4,2,1,2,2}`; reserve(2) keeps 4 slots; a reader mapping sees later increments with no remap; on-disk words after reserve(6) `{5,2,1,3,3,0,0}`.
- Refusals: owner uid+1, mode 0664, size 60, size 4, a hard link, a symlink; size 8 accepted.
- Agent store: fresh start `{1,1}`; publish of revision r gives store word and slot 0 r+1, and the publish hook sees `selected` at revision r with the store word still r (bump after the rename and barrier); `flock(owner.lock)` from outside gets EWOULDBLOCK and a second store throws; restart `{5,5}` then publish `{6,6}`; `change` at 0622 refuses the store; a removed `change` is re-created `{1,1}`.
- Local, RelWithDebInfo: `test_local_change_file`, `test_testing_generation_writer`, `test_local_disk_store`, `test_local_paged_disk_store`, `test_local_publication_retirement` 5/5 pass (`test_local_disk_store` census literals updated for the `change` entry: fresh `{80,3,0}`).
- Mutants (`.scratch/mutants-s5.json`, all KILLED): M1 no startup bump (line 161); M2 bump before the rename (hook sees no `selected`); M2b no bump after publication (line 168); M3 publish skips the store word (line 68); M4 reader accepts any owner (line 105); M5 census does not know `change` (fresh store refuses its own file); M6 reserve may shrink (line 79); M7 ragged size accepted (line 112); M8 bump_all skips sequences (line 77). M1/M2b double as the revert check of the agent wiring.
- ASan/UBSan (`.scratch/build-asan`) and TSan (`.scratch/build-tsan`), separate builds: the same 4 S5 targets 5/5 pass in each.
- Wide local run (`^test_(local|delivery|testing|managed|system_managed|prepared|agent|reconnect)`, 140 tests, -j4): 139 pass, 1 skipped (`test_local_trusted_paths`), 1 fail: `test_delivery_coordinator_reader-pressure` ("pinned reader pressure deadline"). Its disk budget of 1056 bytes fits a fixed set of images; `change` adds 16 bytes to the fresh store, so the budget becomes 1072 (`tests/detent_delivery/test_coordinator.cc:586`). The rerun with 1072 was killed by the stop and is unverified.

## Next (S6)

1. ASan/UBSan and TSan runs of the S6 suite; mutant group B (D5, FIX-2) on the ASan build.
2. `reader_bench` smoke run (functional only).
3. Squash the S6 WIP commits, push `reader/s6`, dispatch CI, watch to green.

## Next (S5, done)

1. Rerun `test_delivery_coordinator_reader-pressure` with 1072 (regex file `.scratch/regex-pressure`); the 1056 failure above is its fails-without-fix evidence.
2. Rebase `reader/s5` onto `Detent:9a5a8c3`, rebuild, rerun the wide set, push (force-with-lease), `gh workflow run ci.yml --ref reader/s5`, watch to green.
3. S6: rebase `reader/s6` onto the final s5; add `add_subdirectory(detent_reader)` to `src/` and `tests/` CMake; make the core's stats counters atomic; commit the untracked S6 files (tests, `reader_bench`, `benchmarks/CMakeLists.txt`, `docs/mechanisms/reader-core.md`); build, test, mutants, sanitizers, CI.

## Design vs code

- S5, the slots: today's store has one table entry (the 176-byte `selected` descriptor, one generation holding every namespace), so `change` has the store word and one sequence, slot 0. Per-namespace slots need namespace-delivery's table (its S7), which carries each entry's slot. `ChangeWriter` takes any slot set and grows by extension, and its test exercises five slots, but the agent bumps slot 0 only. Lead ruling: slot 0 only until namespace-delivery S7.
- S5, `owner.lock`: the design says today's store takes no exclusive lock. It does: `make_owned_storage_session` takes completion's owner lock, `flock(owner.lock, LOCK_EX | LOCK_NB)` (`legacy_file` mode at the pinned completion), for the store's whole life. S5 adds no lock; its test proves the existing one excludes a second store and an outside `flock`. Lead ruling: the existing lock is the exclusive lock; `reader.md` D4 rule 3 and the S5 row now say so.
- S5, accounting: `change` exists before the census, so the census and disk ledger count it like `owner.lock` (a fresh census is two entries and 16 bytes; fresh bootstrap reserves three entries). Lead ruling: a fresh store at `{80,3,0}` is accepted.
- S6, the failure memo: keyed by the store word the failed attempt read (today's one sequence moves with it), not by a per-namespace sequence; with namespace delivery's table it becomes the entry's sequence.
- S6, attach re-reads `selected` (`PreparedReader::attach` loops up to three times) rather than attaching the descriptor the refresh read; the refresh compares generation ids only to reuse an entry.
- S6, `detent_refresh` returns `DETENT_OK` on a refused publish (D5 refusal is not an error) and `DETENT_UNAVAILABLE` when a publish storm outruns attach's retries.
- S6, a record or page ownership failure maps to `DETENT_SOURCE_FAILURE`; only directory trust maps to `DETENT_UNTRUSTED`.
- S6, `detent_read` writes a zeroed `detent_value` on failure (it has no fallback).
- S6, `DETENT_NO_MEMORY` comes from `bad_alloc` and ledger pressure only; the handle's heap and mapping ledgers are unbounded until Q5/S7 sets caps.
- S6, D3's cross-generation segment sharing is not implemented: each generation has its own `PageTable`.
- S6, stats and park windows exist only in the test-only `detent_reader_hooked` build; the release library has neither.
- S6, the open throttle is enforced twice: `remembered()` lets a lookup answer without taking `refreshing`, and `refresh_locked` guards the open itself.
- S5, the legacy `ImagePublisher` over a raw IO (tests only) writes `selected` without `change`; production publication goes through `DiskStore`.

## Fixes found by S6's tests

- FIX-1 (core): the failure memo was keyed by the store word loaded after the failure, so a publish landing during a failed attach hid itself and lookups kept the stale view until the next publish. It is now keyed by the word the attempt read. Regression: `test_reader_core` race case (an `Attaching` window publishes and hides the new generation's metadata mid-attach); with the fix reverted it dies at line 177.
- FIX-2 (`prepared_reader.cc`, predates S6): reading `selected` raced its replacement: a reader that opened `selected` just before a publish renamed a new one over it saw `nlink == 0` and refused the store as `DETENT_SOURCE_FAILURE` ("invalid local generation descriptor ... nlink=0 size=176"). A record found unlinked is now opened again, up to three times, then reported unavailable. Regression: T3 (`test_reader_switch`), whose monitor thread's `detent_refresh` returned 5 before the fix.
- Test expectations corrected (not code): `publish_json` returns the store revision, not the knob value; a released lookup whose sequence moved switches before answering (D4), so the fork and midfree cases now assert the latest revision, and the midfree case gets its zero-count view by making the released lookup lose `refreshing` to a parked holder (a second park window per thread).

## Blocked on

- Nothing.

## Log

- 2026-11-03: Read the design, namespace-delivery D8/D9, the S1-S4 ledger, the store, census, publication and prepared reader. `reader/s5` from `Detent:ff0167d`.
