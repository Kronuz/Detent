> Status: CURRENT (2026-11-04) Ledger for reader steps S5 and S6. Workstream: reader-s5-s6. Design: [reader.md](../design/reader.md) "First steps"; [namespace-delivery.md](../design/namespace-delivery.md) D8/D9 for the `change` shape.

# reader-s5-s6

- Branches / worktree: `reader/s5` from `Detent:ff0167d`; `reader/s6` cut from `reader/s5`. Worktree `~/code/Kronuz/Detent/.claude/worktrees/agent-a1fbf8344cbf48ecb`.
- Lead calls in force: Q1-Q6 all (a) (`docs/status/reader-design.md`); R1-R4 verbatim.
- State: S5 landed (`Detent:d4dbbad`, `Detent:d1e9ee6`). S6 rework on `reader/s6`; current main merged at `Detent:a08386d`. Final merged-tree proof is pending the shared host hold and queued runs. The fixed soak has an unresolved fork-reply timeout; CI is not green.

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
- [x] T3 (switch under 16 readers; injected parks; refusals counted; resources back after close).
- [x] T6 (thread count unchanged; the three fork cases).
- [x] T1 syscall and allocation tests (macOS locally; Linux seccomp in CI).
- [x] `reader_bench` builds and runs (the lead measures).
- [x] Values-based tests, a dying mutant per claim, a revert check per fix, ASan/UBSan and TSan, how-it-works note.
- [ ] CI green on `reader/s6`.

## Done

- `Detent:410cfe9` Add the change file to the agent's store (`src/detent_local/change_file.{hh,cc}`, `disk_store.cc`, `inventory.cc`, `store_core.hh`; test `tests/detent_testing/test_change_file.cc`; note `docs/mechanisms/change-file.md`). Rebased and landed as `Detent:b547256`, `Detent:97da162` (reader-pressure budget 1056 → 1072), `Detent:d4dbbad` (owner.lock wording, rulings).
- `Detent:8c0910d` Split prepared attach from its retirement domain (`PageTable`, `PreparedReader::attach(int directory, ...)`, FIX-2).
- `Detent:8f6da7f` Add libdetent_reader, the lock-free reader core (`include/detent/reader.{h,hh}`, `src/detent_reader/`, `tests/detent_reader/`, `docs/mechanisms/reader-core.md`, FIX-1).
- `Detent:9445d8e` Add reader_bench for reader T1 and T4.

## Proof

S5, `test_local_change_file` (values, not counts):
- Writer shape: `{0,0}` → publish{0} `{1,1}` → reserve(4) `{1,1,0,0,0}` → publish{2,3} `{2,1,0,1,1}` → publish{} `{3,1,0,1,1}` → bump_all `{4,2,1,2,2}`; reserve(2) keeps 4 slots; a reader mapping sees later increments with no remap; on-disk words after reserve(6) `{5,2,1,3,3,0,0}`.
- Refusals: owner uid+1, mode 0664, size 60, size 4, a hard link, a symlink; size 8 accepted.
- Agent store: fresh start `{1,1}`; publish of revision r gives store word and slot 0 r+1, and the publish hook sees `selected` at revision r with the store word still r (bump after the rename and barrier); `flock(owner.lock)` from outside gets EWOULDBLOCK and a second store throws; restart `{5,5}` then publish `{6,6}`; `change` at 0622 refuses the store; a removed `change` is re-created `{1,1}`.
- Local, RelWithDebInfo: `test_local_change_file`, `test_testing_generation_writer`, `test_local_disk_store`, `test_local_paged_disk_store`, `test_local_publication_retirement` 5/5 pass (`test_local_disk_store` census literals updated for the `change` entry: fresh `{80,3,0}`).
- Mutants (`.scratch/mutants-s5.json`, all KILLED): M1 no startup bump (line 161); M2 bump before the rename (hook sees no `selected`); M2b no bump after publication (line 168); M3 publish skips the store word (line 68); M4 reader accepts any owner (line 105); M5 census does not know `change` (fresh store refuses its own file); M6 reserve may shrink (line 79); M7 ragged size accepted (line 112); M8 bump_all skips sequences (line 77). M1/M2b double as the revert check of the agent wiring.
- ASan/UBSan (`.scratch/build-asan`) and TSan (`.scratch/build-tsan`), separate builds: the same 4 S5 targets 5/5 pass in each.
- Wide local run (`^test_(local|delivery|testing|managed|system_managed|prepared|agent|reconnect)`, 140 tests, -j4): 139 pass, 1 skipped (`test_local_trusted_paths`), 1 fail: `test_delivery_coordinator_reader-pressure` ("pinned reader pressure deadline"). Its disk budget of 1056 bytes fits a fixed set of images; `change` adds 16 bytes to the fresh store, so the budget becomes 1072 (`tests/detent_delivery/test_coordinator.cc:586`). The rerun with 1072 was killed by the stop and is unverified.

S6 (`reader/s6`, rebased on `Detent:3e8bdbc`):
- `test_reader_core`: values for every C entry point and the C++ header (`get`, `eval` with hash and switch, `read`, `revision`, `refresh`, moves); D2 (open attaches nothing: `attached == 0`, first lookup 1); a namespace list refuses outside names; a hidden `.read` keeps the installed view and is remembered until the word moves or a refresh; the failure-memo race (FIX-1); Q6: an absent store opens, a complete store renamed into place right after a failed try still answers unavailable, and answers 8 after 250 ms; an untrusted directory is `DETENT_UNTRUSTED`; `selected` without `change` is `DETENT_SOURCE_FAILURE`.
- `test_reader_switch` (T3/T6): 16 readers, non-decreasing values bounded by the publisher's ceiling, only `DETENT_OK`, live views at most 4, descriptors and file mappings back to their pre-open counts after close, thread count unchanged; parks: BeforePin across four publishes (retries on the newest view), the recheck window (`Freeing`: the reader pins the new slot, the freed slot has 0 pins), Pinned across six publishes (3 refused, counted; the pinned view answers); forks: a parked reader, an idle `refreshing` holder and a publish parked after `free_view`, each child seeing 10 of 10 publishes.
- `test_reader_syscalls`: T1 on the release library (macOS `TASK_EVENTS_INFO` delta, Linux seccomp in CI).
- Local release run on the rebased tree, wide set plus reader tests: 141 of 142 pass, 1 skipped (`test_local_trusted_paths`, as on main). Local T3 at 20 s / 1000 generations: pass in 311 s (a GenerationWriter publish costs about 0.3 s here, mostly fsync).
- ASan/UBSan (`.scratch/build-asan`): the 13-test S6 set passes; switch phases at 10 s / 100 generations: T3 21,243,996 lookup pairs, 0 refused, 0 unavailable, max live views 2. UBSan caught a test bug (`detent_status(99)` is outside the enum's range; now 15).
- TSan (`.scratch/build-tsan`): switch, parks and forks pass; T3 7,085,644 lookup pairs, 0 refused, max live views 3. TSan's own thread, started on the first `pthread_create`, is excluded by creating and joining one thread before the baseline.
- Mutants (all KILLED): D5-1 pin without the recheck (line 302, the `Freeing` case); D5-2 publish without the next-slot check (line 298); D5-3 publish frees no old slot (line 283); D10-1 child keeps the parent's counts (line 388); D10-2 child keeps `refreshing` held (line 388); D10-3 prepare does not take `refreshing` (line 441); D2-1 open attaches (line 60); D4-1 stale lookup never switches (line 112); Q6-1 no open throttle, both checks (the renamed store answers ok); FIX-1 reverted (line 177); FIX-2 reverted (T3 monitor gets status 5, line 227); D4-2 reader accepts any `change` owner: killed by `test_local_change_file` (S5 M4), since the reader tests cannot make a foreign-owned file without root.
- `reader_bench`: `--generate 1000` and a 1,000-call, 2-thread run of both readers complete and print 0 allocations in the timed loop (functional only; the lead measures).

## Next

S6 criteria: D1, D2, D4, D5, D10 and the C++ header, attach by directory descriptor, Q6 (a), T3, T6, T1 syscalls, `reader_bench`, proof (values, mutants, reverts, sanitizers, note) are met locally; CI green on `reader/s6` is not.

1. Read final-code CI [41974132576](https://github.com/Kronuz/Detent/actions/runs/41974132576), dispatched on `Detent:31ac047` (pending at recovery handoff). It supersedes merged-only run 41973979278 at `Detent:a08386d`; per-ref CI concurrency cancels the earlier run. Earlier CI 41933091120 is **cancelled**: linux-tsan cancelled, all other executed jobs successful; freebsd and headers skipped by plan. It is not green evidence.
2. Build and run the new deterministic `replaced_selected` case in `test_reader_core`, then the final-form FIX-2 revert (`.scratch/s6-final-fix2.json`). This replaces `checked_or_unlinked` with `check_generation_file` for records. Rebuild the restored source and require the test to pass again. None of this new proof has run yet.
3. Investigate the fixed-head concurrent soak failure: `.scratch/overnight/done/detent-s6-soak-fixed/x4-1-4.log` in the main worktree records T3 passing 792,825,041 lookup pairs and 1,000 generations in 796.448 s, then a fork child reply timeout at `test_reader_switch.cc:390`. Alone: 0/12 failed; concurrent: 1/24 failed. The failure is after T3, and its cause is unverified. New diagnostics identify fork scenario, PID, expected revision and poll result; they preserve the 30-second deadline. No surviving process from that run was found at recovery.
4. Run T3 under each separate sanitizer build at defaults (60 s / 1,000 generations), `ctest -R reader_switch --repeat until-fail:20`. Queue bounded individual repetitions under light holds of at most 30 minutes, and stop that flavor on failure. Full criteria are unchanged.
5. Run the merged-tree reader suite and dispatch CI on the final code. Current local compilation is blocked by Xapiand's exclusive hold through 2026-11-05 01:21 PST. The shared run queue handles runs after that hold.
6. Queued jobs are fully staged in the main worktree's `.scratch/overnight/queue/`: `detent-s6-final-proof.sh`, then `detent-s6-t3-gate.sh`. The first builds release, runs `test_reader_core`, invokes `/usr/local/bin/python3 .scratch/mutants.py .scratch/build .scratch/s6-final-fix2.json`, requires `KILLED rc=`, reruns the restored baseline, checks syscalls, runs six rounds of four `forks` processes, builds each sanitizer and runs its core test. The second runs the 20 default T3 trials per flavor as separate `ctest -R '^test_reader_switch$'` invocations, equivalent to stopping at the first failure in `--repeat until-fail:20`. Every command uses a light hold of 30 minutes and a 1,750-second process timeout; CTest retains its 900-second test timeout. Each build has at most four jobs.
7. Read queue driver results in the main worktree's `.scratch/overnight/done/detent-s6-final-proof.{log,rc}` and `detent-s6-t3-gate.{log,rc}`. Per-command logs are in this worktree's `.scratch/s6-final-proof/`; `preflight-passed` is created only if the first job completes. Both jobs reject code changes relative to `Detent:31ac047`. The first job restores `prepared_reader.cc` even if a mutant times out. Source snapshot: `.scratch/s6-final-proof/prepared-reader-original.cc`. Do not edit the affected source during that job. Tests, mutants and the new hook remain **unverified** until these logs exist and pass.
8. Request the independent proof audit and security review from the lead once the required proof is complete. The code review and landing are the lead's. Do not rebase or force-push.

Local commands: builds `quiet light NAME MINUTES "what" -- <script>`; `.scratch/reader.sh BUILD OUT SECONDS PUBLISHES` (reader tests), `.scratch/san6.sh asan|tsan` (S6 set under a sanitizer, needs `.scratch/build-<flavor>`), `.scratch/phases.py BUILD 10 100 400` (switch, parks, forks one by one), `python3 .scratch/mutants.py BUILD SPEC.json`.

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

- Local builds and tests: the shared exclusive host hold through 2026-11-05 01:21 PST. Jobs are queued; no local build or test ran during recovery.
- The prior fork child reply timeout is unresolved. S6 is not ready for independent review or landing.

## Log

- 2026-11-03: Read the design, namespace-delivery D8/D9, the S1-S4 ledger, the store, census, publication and prepared reader. `reader/s5` from `Detent:ff0167d`.

- 2026-11-04: Recovered the branch and raw fixed-head soak evidence. Merged current main at `Detent:a08386d`, retaining main's `VerifiedPages` mechanism and HTTP removal. Pushed the branch and dispatched CI 41973979278. Added deterministic replacement proof and fork timeout diagnostics; unbuilt while the host is exclusively held.

- 2026-11-04: `Detent:31ac047` commits the deterministic FIX-2 regression and fork scenario/PID diagnostics. Pushed and dispatched CI 41974132576; staged both bounded proof jobs. Local verification remains pending the exclusive hold; the lead resumes the worker when queued proof and CI have results.
