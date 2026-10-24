# Code review: `flat-on-tip` (496e8f3, commits 7915d9f..496e8f3)

Reviewer: code-reviewer. Tier: new public core container plus a shared public API change (four model field types). One review, one fix round.

## Verdict

**REJECT** (one blocking item, a narrow fix). The container, the error precedence, the merge rewrites, the drains and the decision numbers hold up. A hostile 4.8 MB snapshot or patch takes 5.4 s to parse instead of 45 ms, because one `reserve()` call per `"overrides"` member makes parsing quadratic (CR1). The fix is one line plus a re-run of the probe. Everything else is a condition or a note.

## Evidence run

- Build: `/Users/gmendezb/code/btree/.scratch/review-flat-build`, Release, `DETENT_BUILD_AUTHORITY=ON`, macOS arm64, Apple clang, `-j4`, every compile and run inside `quiet light flat-review` at nice 19. Benchmarks were first OFF; a later reconfigure with benchmarks ON compiled 13 benchmark targets (model_lookup, model_footprint, model_lifecycle, update, agent_shm, scale, authority, command_work, authority_durable, distribution_codec, local_image, paged_snapshot, read). None was run.
- `ctest -j4`: 211/211 passed, 148.9 s.
- Differential probe against the std::map tree (`/Users/gmendezb/code/btree/detent`, 7915d9f): 30,000 rounds, 679,376 output lines, `cmp` identical. A UBSan build of the flat arm (3,000 rounds) is identical to the std::map output. `test_duplicate_precedence` and `test_flat_map` pass under UBSan.
- Mutants of `json_schema.cc` and `json_patch.cc` against `test_duplicate_precedence`: three of four killed (CR2).
- Quadratic probe: CR1, CR3.
- Overload probe (`overloads.cc`): builds with `-Wall -Wextra` and UBSan, passes.

## Criteria

| Criterion | Result | Evidence |
| --- | --- | --- |
| Error messages and precedence unchanged (duplicate knob, duplicate override, invalid name, definition errors) | Met | `diff_probe.cc`: 5,470 duplicate-knob and 8,968 duplicate-override rejections, 15,758 invalid-name rejections, all identical to 7915d9f. Reading: every error exit in `json_schema.cc:332-352`, `json_patch.cc:107-125` and `json_schema.cc:137-144` runs the duplicate check over the entries pushed so far, which is exactly the set the old per-entry insertion had seen. |
| `apply_patch`, `diff_snapshots`, `compose_patches`, `merge_over_defaults` equal the old results, including `max_knobs` rejection and version fields | Met | Same probe: 1,228 `max_knobs` rejections (reason Oversize) and 28,771 applied results identical; `diff` then `apply` round trip equals the target on every pair. |
| CLI override parse unchanged | Met on results, not on cost | Probe output identical; cost in CR3. |
| Builder permutation, duplicate detection | Met | Cycle walk at `flat_map.hh:352-366` is correct by reading (`order[j]` is the source of slot `j`; each visited slot resets to `j`). 300 builder rounds plus 200 shuffled parses plus the 30,000-round probe; UBSan clean. |
| Strong guarantee of every insertion path | Met for insert and try_emplace; basic for `insert_or_assign` assignment branch and for copy-assign | `test_model_alloc_failure` sweeps insert, erase, copy, assign at every allocation position of all four types (passes). The assignment branch (`flat_map.hh:244-247`) assigns `T` in place and has no production caller (grep of `src/`, `include/`, `examples/`). CR6. |
| Heterogeneous erase, `operator[]`, `try_emplace`, copy assignment | Met | `overloads.cc`: `erase("lit")`, `erase(string_view)`, `erase(string)`, `erase(iterator)`, `erase(const_iterator)`, `operator[]("lit")`, self-assignment, aliasing on try_emplace and insert_or_assign, non-transparent comparator. |
| Iterator invalidation at call sites | Met by reading | Cursors in `state.cc:445,1372`, `change_history.cc:95`, `export_encoder.cc:38`, `read_reply.cc:126` iterate definitions that no step mutates while the cursor lives (immutable shared records or a command's own value until `clear`). The four drains mutate only owned values and re-read the end each turn. Vector move and swap keep the buffer, so iterators follow the buffer as node iterators did. No new site found. |
| Drains keep the per-turn charge and do not finish early | Met | The diff changes one expression per site (`state.cc:710,788,1081,1244`); the claim, the charge and the loop condition are untouched. `erase(prev(end()))` is noexcept and allocates nothing. |
| No remaining quadratic production path on hostile input | **Not met** | CR1 (JSON), CR3 (CLI overrides). Wire import (`state.cc:139-146`, `656`) is strictly ordered by `previous < key`, so it appends; `State::snapshot` (`state.cc:1641`) iterates a sorted map and appends. |
| Docs and decision numbers trace to `model-container-three-way-20261024.json` | Met with notes | Heap 14.4/65.8, 17.0/68.4, 24.8/76.2 MB and CPU 112/142/221 and 477/542/601 ms match the JSON. Ratios in `docs/perf/flat-map.md` (0.58, 0.86, 0.85, 0.77, 0.96, 0.94; apply 0.16/0.15/0.41/0.70; diff 0.07/0.06/0.10/0.28; erase maxima 84-1084 and 416-8750 ns; prune 7.5 s) match `flat-map-exploration-20261023.json`. CR4, CR3 (one false sentence). |

## Findings

### CR1 (blocking): repeated `"overrides"` members make parsing quadratic

`json_schema.cc:93`: `override_builder.reserve(override_builder.size() + it->value.MemberCount())` runs once per `"overrides"` member. `std::vector::reserve(n)` allocates exactly `n`, so a knob that repeats `"overrides"` with one entry each reallocates and moves the whole table every time. The parser accepts repeated members (the old code accumulated them, and `test_duplicate_precedence.cc:117` covers a duplicate across a repeated member). The std::map code was O(n log n) here.

Probe `probes/quad.cc` (K members of `"overrides":{"gNNNNNNN":true}` in one bool knob, `parse_defaults`, 1 GiB byte cap so the size limit does not mask it):

| K | bytes | std::map 7915d9f | flat-on-tip |
| --- | --- | --- | --- |
| 5,000 | 150 KB | 1.2 ms | 9.8 ms |
| 20,000 | 600 KB | 5.0 ms | 88 ms |
| 40,000 | 1.2 MB | 11.0 ms | 336 ms |
| 160,000 | 4.8 MB | 45 ms | 5,400 ms |

Time grows 16x per 4x members. At the default 16 MiB `max_bytes` (about 560,000 members) the extrapolated cost is about 60 s of one core on the update thread; this extrapolation is not measured. The control (one `"overrides"` object with 160,000 descending keys) takes 10.8 ms, so the builder itself is fine.

Fix: drop the per-member reserve, or reserve only when `size + count > capacity` and then to `max(size + count, 2 * capacity)`. Put the guard in `flat_map_builder::reserve` so no caller can reintroduce it. Re-run `quad` and add a regression test with at least 20,000 repeated members and a time bound. The same trap does not exist in the knob-level builders: they reserve once, bounded by `max_knobs` (`json_schema.cc:337`, `json_patch.cc:95`, and the patch count check at `json_patch.cc:83`).

### CR2 (condition C1): one precedence mutant survives

Mutant `mutants/m2` (`json_patch.cc`, remove the duplicate check before `invalid knob name`) passes `test_duplicate_precedence`. The test has `[a, bad_name, a]` and `[a, b, a, bad_name]` for snapshots (`:60-62`) but no `[a, b, a, bad_name]` for patches (`patch_names()`, `:85-93`). Mutants m1 (snapshot definition error), m3 (override duplicate versus a later member error) and m4 (patch definition error) are killed (`:59`, `:105`, `:88`). The probe shows the code is correct today; the test does not guard it. Add the patch case.

### CR3 (condition C2): `parse_cli_overrides` is a one-at-a-time insertion into a sorted vector, and the record says no such path exists

`overrides.cc:24-38` does `find` then `values[name] = ...` per argument. Descending names hit the front of the vector every time. Probe `quad.cc`, K descending `k:NNNNNNN=1`: 5,000 in 11.8 ms (std::map 1.2 ms), 40,000 in 760 ms (12 ms), 160,000 in 11.9 s (50 ms). `Client` calls it on `options.cli_overrides` (`client.cc:98`); the bindings pass a vector, so the count is not bounded by an argv limit in every embedding. `docs/perf/flat-map.md` says "No Detent production path does that after the call-site changes". That sentence is false for this path. `CONTAINER_SELECTION_ARCHITECTURE.md` item 3 states the cost without listing it.

Fix one of two ways: build the table with `flat_map_builder` and keep the error order (malformed, invalid name, duplicate, invalid value, each in input order; the duplicate check must run on each earlier error exit, as `json_schema.cc:332-352` does), or bound `cli_overrides.size()` and correct the sentence. Add the descending case to a test either way.

### CR4 (condition C3): benchmark provenance and one stale count

`model-container-three-way-20261024.json` `arms.flat` names `explore/flat-map 3ec0df4`, not 496e8f3. `git diff 3ec0df4 496e8f3 -- src include` shows only `client.cc` (publication order and a `static_assert`) and two comments, so the model, builder and merge code that was measured is the code under review. Say so in the record. `docs/perf/flat-map.md` ("Identity") says ctest 210/210; the branch has 211. The binary hashes in the JSON cannot be re-checked against this tree without rebuilding the explore branch; I did not.

### CR5 (non-blocking): wire decode appends without `reserve`

`state.cc:139-146` and `state.cc:656` emplace overrides one at a time after the count is known and bounded (`limits.overrides`, 256 by default). Capacity ends up up to 2x size (three overrides: capacity 4), which the heap numbers in the record (JSON parse path, exact reserve) do not show. A growth reallocation also moves the table inside one budgeted turn. At 256 entries this is small. Add `reserve(count)` after the `require(count <= limits.overrides)` at `:137` and at the phase that reads the count (`:630`).

### CR6 (non-blocking): guarantees to state in the header

- `insert_or_assign` assignment branch (`flat_map.hh:244`) gives only the basic guarantee when `T`'s assignment can throw; the header comment promises the strong guarantee for insertion only, which is true, but a reader will assume it for assignment. Say so.
- Copy assignment is the basic guarantee (the `assign` leg of `test_model_alloc_failure` asserts a valid sorted target only). It matches the earlier review's C4 position for `Client::try_apply_source_*`.
- `flat_map_builder::finish()` leaves the map in input order when it returns an index; the contract is in the header and the callers discard the map.

### CR7 (non-blocking): fragile and slow details in the merges

- `apply_patch` (`json_patch.cc:139-146`) copies `version`, `published_at`, `source_id` by hand. A later `Snapshot` field is dropped silently where the old code copied the whole base. Add a `static_assert` on the field list or copy the metadata through one helper.
- Merge callbacks call `emplace(e)`, which builds a `value_type` copy and moves it into the vector (two constructions). `emplace_back(e)` after the sorted-order guarantee would copy once.

### CR8 (non-blocking): other one-at-a-time erase and insert sites outside production

- `benchmarks/container-alternative/model_footprint.cc:112` erases 75,000 keys one at a time from a 100,000-entry table; expect seconds, as the record's prune row (7.5 s). It is built by default (`DETENT_BUILD_BENCHMARKS=ON`) but not run by ctest. Not run by me.
- `tests/detent_authority/test_state.cc:345` erases `begin()` (test, 256 entries).
- `examples/publish_snapshot.cc:55-96` inserts one entry per CLI argument; the references returned by `as_bool` are used before the next insert.
- `benchmarks/btree-repair/*.cc` are not in CMake and still target btree (earlier review CR4); not part of this change.

### CR9 (non-blocking): public API reference

`website/src/content/docs/reference/cpp-api/client.md` shows the new field types but does not say that `value_type` is `pair<Key, T>` (key is not const) and that any insert or erase invalidates every iterator and reference. The header comment states both. Link the header from the reference.

## Blocking items

- CR1: fix the per-member `reserve` in `json_schema.cc:93` (and guard `flat_map_builder::reserve`), add a regression test, and re-run `probes/quad.cc`.

## Conditions

- C1: add the `[a, b, a, bad_name]` patch case (CR2).
- C2: fix or bound `parse_cli_overrides` and correct the false sentence in `docs/perf/flat-map.md` (CR3).
- C3: record that the benchmarked arm is 3ec0df4 and correct the 210/211 count (CR4).
- C4: land only after the Linux GCC 11 ASan/LSan full-suite run in progress elsewhere passes.

## Probes and harnesses (in `/Users/gmendezb/code/btree/review-flat-wt/.scratch/review-flat/`)

- `probes/diff_probe.cc` (differential; `diff_old`, `diff_new`, `diff_ubsan`, outputs `old.out`, `new.out`, `ubsan.out`, `old3000.out`).
- `probes/quad.cc` (`quad_old`, `quad_new`).
- `probes/overloads.cc` (`overloads`).
- `probes/build_probe.sh <tree> <source> <out> [flags]` (compiles a probe against a tree's `include/` and `src/detent/{json_schema,json_patch,overrides}.cc`).
- `mutants/m1..m4` (source copies with one duplicate check removed each; `probes/dup_m1..dup_m4` binaries; `probes/dup_ubsan`, `probes/flat_ubsan`).
- Build tree outside the branch: `/Users/gmendezb/code/btree/.scratch/review-flat-build`; logs `review-flat-ctest.log`, `review-flat-bench-build.log` in `/Users/gmendezb/code/btree/.scratch/`.

## Host-rule disclosure

Every compile, test run and probe ran inside `quiet light flat-review 60` at nice 19 with `-j4` at most, after a `quiet check` that returned exit 2 (light holds by other sessions) each time. No exit 1 was seen. One wrapper command failed to parse (`sh -c` with process substitution) before starting any compile; I reran it without. No benchmark, no fuzz job, no remote host.

## Not run

- Linux, GCC 11, ASan, LSan, TSan (ASan hangs at startup on this Mac; UBSan alone was used on `flat_map.hh`, the parsers and the probe).
- Any benchmark; the 13 benchmark targets were compiled, not run. Binary hashes in the three-way JSON were not matched to a rebuild.
- Allocation-denied authority cleanup beyond what `test_model_alloc_failure` and `test_client_publication_failure` cover (both pass inside the 211).
- A 16 MiB hostile document (the CR1 figure at that size is extrapolated from 4.8 MB).
- Python and Rust bindings; the fleet-corpus canonical-bytes comparison; regeneration of the owner's canonical JSON/shm files (I did not `cmp` them; the differential probe compares parsed and serialized output on random inputs instead).
