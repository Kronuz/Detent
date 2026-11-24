> Status: CURRENT (2026-11-23) APPROVED by the owner on 2026-11-04, who ruled Q1 (a) and Q2 (a) (see "Owner rulings"); restated on the landed reader ABI (one int64 knob, D65; the mapping cap, D143) after the design review `legacy-h-design.md` F1-F4. Refines [reader.md](reader.md) D7, S8 and S9 for legacy wave 2 step H (quest `legacy-h`).

# Client local source in the bindings

Python and Rust read managed generations through the reader core, the same `detent::Reader` C++ uses, and step H deletes the Client that `make_local_read_source` plugs into.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-04. Ledger: `docs/status/client-local-source.md`.
- Base: `Detent:3e8bdbc` (main). Reader core citations are at `reader/s6`, `Detent:70f648f`; Step H at `legacy/wave2-f`, `Detent:d18d6f5`.
- Depends on: reader.md R1 (defaults per call), R2 (no overrides), R4 (explicit handles), D1 (the C ABI), D9 (the test writer), and the lead's (a) calls on its Q2 and Q6. Needs reader S6 on main.

## Owner rulings

Ruled 2026-11-04, verbatim: "Delete Detent::Client and health() if they're legacy."

- **Q1 (a).** Step H deletes `detent::Client`; `Reader` is the one read API in C++, Python and Rust.
- **Q2 (a).** `health()` is dropped; `revision(namespace)` and `lookup`'s status are the diagnostics.

## Abstract

The owner ruled today to drop the unmanaged agent and remove the Client's legacy sources: `source_path`, which polls a snapshot file, and `source_poll`, which takes a user function returning snapshots or patches (`include/detent/client.hh:62,99-101`). Both bindings build a `detent::Client` from a defaults file, an optional `source_path`, CLI overrides and a poll interval (`bindings/python/detent_python.cc:48-70`, `bindings/rust/src/shim.cc:25-38`). After H the only source left is `read_source`, filled by `make_local_read_source` (`include/detent_local/client_source.hh:31`).

**Recommendation: bind the reader core, not the Client.** R1 removes the Client's baked defaults and R2 its CLI overrides; what remains over a local source is a refresh thread (`src/detent_local/client_source.cc:113-128`) that the core replaces with one load per lookup. reader.md already moves Python and Rust onto the C ABI (S8, S9) and deletes `Client` in H (`reader.md:312,323-324`), so binding `make_local_read_source` now writes each binding twice. This design fixes the shared shape, mirroring `detent::Reader` (`include/detent/reader.hh:29-90` at `reader/s6`), and lists what H deletes. Nothing here is measured; speed is reader.md T1's row, which the lead measures.

## Decisions

### D1. One type: `Reader`, in all three languages

| | C++ (`reader.hh`) | Python (`detent`) | Rust (`detent`) |
| --- | --- | --- | --- |
| Open | `Reader(store, publisher_uid, namespaces = {}, mapping_bytes = 0)`, throws `ReaderError` | `Reader(store, publisher_uid, namespaces=(), mapping_bytes=0)`, raises `ReaderError` | `Reader::open(store, publisher_uid, &[..], mapping_bytes) -> Result<Reader, ReaderError>` |
| Value | `get(name, fallback, hash, switch_value) -> int64_t` | `get(name, fallback, hash=None, switch_value=None) -> int` | `get(&self, name, fallback: i64, hash: Option<&str>, switch_value: Option<&str>) -> i64` |
| Typed | `get_as<T>(name, fallback, hash, switch_value) -> T` | `get_bool(name, fallback, hash=None, switch_value=None) -> bool` | `get_as<T: Knob>(&self, name, fallback: T, hash, switch_value) -> T` |
| Status | `lookup(name, fallback, hash, switch_value) -> Lookup{status, value}` | `lookup(name, fallback, ...) -> Lookup` (`.status`, `.value`, truthy on `OK`) | `lookup(&self, name, fallback, ...) -> Result<i64, Status>` |
| Refresh | `refresh() -> detent_status` | `refresh() -> Status` | `refresh(&self) -> Result<(), Status>` |
| Revision | `revision(ns) -> optional<uint64_t>` | `revision(namespace) -> int \| None` | `revision(&self, ns) -> Option<u64>` |
| Close | destructor | deallocation | `Drop` |

Every knob is one int64 read through one call, `detent_get(handle, name, hash, switch_value, fallback, &out)` (one-knob-type, D65); a flag is a knob holding 0 or 1. The arguments are the C ABI's `detent_open_options` (`include/detent/reader.h`):

- `store` is the agent's store directory.
- `publisher_uid` is required, because trust is never inferred from the directory owner (reader.md D1 item 5). An application passes the uid its agent runs as.
- `namespaces` limits the handle to those namespaces. Empty means all of them, including ones published later.
- `mapping_bytes` caps the bytes of mapped packs and their verified bitmaps (D143); 0 means 512 MiB, and a cap below one pack and its bitmap (`DETENT_MINIMUM_MAPPING_BYTES`) fails the open with `INVALID_ARGUMENT`.

There is no defaults path (R1), no override list (R2) and no poll interval: a lookup sees a publish through the mapped `change` file (reader.md D4), and `refresh()` pays the attach earlier from a thread the caller chooses.

Differences, each forced by the language:

1. Rust's fallible constructor is `open` returning `Result`, after `detent_open` and `File::open`; Rust has no default arguments, so `mapping_bytes` is explicit.
2. Rust `lookup` is `Result<i64, Status>`: the caller holds the fallback, so the error side needs only the status. Python's `Lookup.value` is the int, the fallback on any error.
3. Typed reads mirror C++ `get_as<T>`: the fallback when the value is outside `T` (for `bool`, other than 0 or 1). Rust's `get_as<T: Knob>` takes `bool` and the fixed-width integers through a sealed trait. Python ints have no width, so only the flag read remains: `get_bool`.
4. `store` takes `os.PathLike | str | bytes` in Python and `impl AsRef<Path>` in Rust, passed as raw bytes; a `&str` would refuse non-UTF-8 paths the C ABI accepts.
5. Python checks types first: a non-`bool` fallback to `get_bool`, a non-`int` or `bool` fallback to `get` or `lookup`, or a non-`str` name raises `TypeError`; an integer outside int64 raises `OverflowError`. C++ and Rust reject these at compile time.
6. Only C++ keeps `native()`, for C callers; the bindings have none.

### D2. Errors: open raises, lookups never do

Only opening fails loudly: `ReaderError` carries the `detent_status`, with the message `detent_open: <detent_status_name>` as in C++ (`reader.hh:42-44`). Under reader Q6 (a), opening a store that does not exist yet succeeds, and lookups return the fallback with `UNAVAILABLE` until a generation appears (`src/detent_reader/core.cc:614-624`). Every lookup returns the caller's fallback on any error, and `lookup` reports the status (reader Q2 (a), the lead's call; Python's raising today is the owner's to keep or drop). `Status` is a Python `IntEnum` and a Rust `#[non_exhaustive]` enum with `Unknown(u32)`. The C ABI may add statuses without a version bump ("known or not, is an error", `reader.h:23`), so Rust declares the C return type as `c_int` and maps it: reading an out-of-range value into a Rust enum is undefined behavior. Python maps an unknown value to a pseudo-member that keeps the integer. Both check `detent_abi_version` once: Python at import, raising `ImportError`; Rust in `open`, as C++ does in its constructor. The version does not move when statuses are renumbered (reader S6 renumbered them at version 1), so neither binding trusts its own numbering: Python builds `Status` from the header's `DETENT_*` constants compiled into the extension, and a Rust unit test checks that `Status::from_raw(n).name()` equals `detent_status_name(n)` for every known value.

### D3. Lifetime: the object is the handle, with no `close()`

A close that races a lookup is undefined (reader.md D1 item 7), so in every language the handle closes when the object ends and never earlier. Rust lookups borrow `&self`, so `Drop` cannot overlap one; `Reader` is `Send + Sync`, not `Clone`, shared through `Arc`. In Python every lookup holds a reference to the reader, so deallocation cannot overlap one either; a `close()` or context manager would bring the race back, so there is none.

Python opens in `tp_new`, with no `__init__` and no subclassing, so the handle never changes after construction. Lookups keep the GIL (reader.md D7); open and `refresh()` release it around their I/O. The core's lookups are thread-safe and the object is immutable, so a free-threaded build would need no lock; the extension declares `Py_MOD_GIL_NOT_USED` once a free-threaded run passes T3 and T4 (follow-up). A forked child keeps a working handle (reader.md D10). No language starts a thread.

### D4. One scenario table, three runners

`bindings/tests/scenarios.json` lists each case (operation, arguments, expected value and status), and a C++ runner over `detent::Reader`, the Python tests and the Rust tests all read it, so a language that drifts fails. Each publishes the same knobs through the test generation writer, which writes real generations into a store (reader.md D9, on main; C face `include/detent_testing/generation_writer.h:14-19`).

### D5. Old call to new call

| Old (binding or C++) | New | Why |
| --- | --- | --- |
| `detent.Client(defaults_path)`; `Client::new(defaults, None, vec![], ms)` | `detent.Reader(store, uid)`; `Reader::open(store, uid, &[])` | R1: defaults move into each call |
| `Client(defaults, source_path=p)`; `Client::new(d, Some(p), ..)` | Same `Reader`, over the managed agent's store | The file source is removed (owner, 2026-11-04) |
| `cli_overrides=[...]` | None. Tests publish values with the writer | R2 |
| `poll_interval_ms=N` | None; `refresh()` to attach earlier | reader.md D4, Q1 (a) |
| `get(name, hash_val=, switch_val=)` | `get(name, fallback, hash=, switch_value=)` | R1 |
| `get_bool(name, hash_val=, switch_val=)` | Python `get_bool(name, fallback, ...)`; Rust `get_as(name, fallback, ...)` | R1; mirrors C++ `get_as<bool>` |
| `except DetentError` around `get`; Rust `?` | `lookup(name, fallback).status`; Rust `lookup(..)?` | reader Q2 (a) |
| `except DetentError` around construction | `except ReaderError as e: e.status` | D2 |
| `health()` | `revision(namespace)` and `lookup(name, fallback).status` | Q2 (a), owner |
| C++ `InitOptions{.read_source = make_local_read_source(opts)}`, `make_managed_read_source(dir, uid)` | `detent::Reader(dir, uid, namespaces)` | Q1 (a), owner |
| C++ `auto_refresh = false` with `LocalReadSource::refresh()` | `Reader::refresh()`; there is never a thread | reader.md D5 |

### D6. What the C++ side must provide

1. **`libdetent_reader` as a shared library exporting only `detent_*`**, every C++ symbol hidden (a version script on Linux, an exported-symbols list on macOS), its C++ dependencies linked in. Today it is `add_library(detent_reader core.cc)` with `detent::local_view` PRIVATE (`src/detent_reader/CMakeLists.txt:3-6`), static by default; cargo cannot follow CMake's link interface, which is why `build.rs` hand-lists eight include directories and three archives (`bindings/rust/build.rs:26-48`). With the shared library `build.rs` is a link search, `dylib=detent_reader` and an rpath, and cargo compiles no C++. The Python extension links the same file.
2. **`libdetent_testing` as a shared library** exporting only `detent_testing_*`, with a header-only C++ face (`detent::testing::Store`, `include/detent_testing/store.hh`) over its C face. It and `libdetent_reader` install as the opt-in `development` component (`DETENT_INSTALL_TESTING`, `cmake --install --component development`, package `Detent` with `detent::reader` and `detent::testing`), which replaces `TestKnobs` and `TestGuard` for application tests (D149). Never linked into a shipped binary.
3. **Nothing new in the ABI** for the surface above; Q2 (a) adds no health call.
4. **Step H deletes** (Q1 (a)), with the callers each deletion leaves (grep of `src`, `include`, `tests`, `examples`, `benchmarks`, `tools`, `bindings` at `Detent:3e8bdbc`):

| Deleted | Files | Callers H ports to `Reader` or deletes |
| --- | --- | --- |
| `Client`, `InitOptions` (`source_path`, `SourcePayload`, `source_poll`, `source_id`, `on_update`, `read_source`), `HealthSnapshot`, `Metrics`, `Client(Snapshot)` | `include/detent/client.hh`, `src/detent/client.cc` | `examples/demo.cc`, `tests/cmake_consumer/main.cc`, `tools/w5/w5_detent.cc`, `benchmarks/container-alternative/model_lookup.cc`, `benchmarks/detent_read_bench.cc`, `src/detent_agent/main.cc` (`detent get`, reader S7), `src/detent_agent/agent.hh` (legacy G), the test table below |
| `ReadSource`, `ReadSourceFactory`, `ReadSourceStatus`, `ClientReadView`, `EvaluationView` | `include/detent/read_view.hh`, `src/detent/read_view.cc` | Only `client.hh` and `client_source.hh` |
| `LocalReadSource`, `ClientSourceOptions`, `make_local_read_source`, `make_managed_read_source` | `include/detent_local/client_source.hh`, `src/detent_local/client_source.cc` | `main.cc`, `test_client_source`, `test_lazy_page_access`, three system probes |
| `detent::global`, `TestKnobs`, `TestGuard`, the test slot | `include/detent/global.hh`, `src/detent/global.cc`, `include/detent/testing.hh`, `src/detent/testing.cc`, `include/detent/detail/test_hook.hh`, `src/detent/test_hook.cc` | `tests/detent/test_testing_guard.cc` |
| `SnapshotRef` | `include/detent/snapshot_ref.hh` | `tests/detent/test_concurrency_stress.cc` |
| Override precedence and the first evaluator, `detail::evaluate` (reader.md D6); `detail::ramp`, the one percentage rule, stays for the paged evaluator | `include/detent/detail/evaluation.hh` (keeps `ramp` only) | `test_evaluation` and `test_one_knob_differential` compare the paged evaluator with a reference written in the test or with recorded answers; `one_knob_record.cc` builds only against the commit it records (`one_knob_record.sh`) and stays; the c3 mutant moves from `read_view.hh`'s `convert` to `reader.hh`'s `get_as<bool>` |
| `OverrideTable` as a read layer (R2) | `include/detent/overrides.hh`, `src/detent/overrides.cc` | Also used by `test_duplicate_precedence`, `test_model_alloc_failure`, `benchmarks/btree-repair/actual_type_sweep.cc`, `w5_detent.cc`: H checks each, and deletes the file only if none needs it outside the Client |
| The pybind11 module and the cxx shim | `bindings/python/detent_python.cc` (rewritten in B3), `bindings/rust/src/shim.hh`, `bindings/rust/src/shim.cc` | B3, B4 |

`json_patch.hh` stays: the authority benches and tests use it.

### D7. A namespace's entry, from the agent's write to the lookup's answer

The agent gives each namespace an entry in `selected` and a slot in `change` (namespace-delivery D8, reader.md A1-A2). A publish grows `change` to cover every slot the new table names, renames `selected`, then bumps each changed slot's sequence and last the store word (D4 rules 1 and 4). A new namespace takes the slot past the highest slot of the entries the cycle keeps, so a dropped namespace's slot returns to the next name added. A lookup reads its namespace's entry in the handle's installed view and compares one word with the value it saw before the view's table was read; a moved word makes it refresh (`docs/mechanisms/reader-core.md`). An owner ruling (D155) keeps reader.md D4: a namespace that cannot attach answers its last good generation with OK, and only `detent_refresh` reports the failure.

| Transition | The agent bumps | The lookup watches | The lookup answers | Test (`test_reader_core`) |
| --- | --- | --- | --- | --- |
| Generation published (the entry names a new generation) | the entry's slot, then the store word | the entry's slot | the new generation, from the lookup after the bump | `per_namespace` (`u` alone moves; `t` answers without a refresh) |
| Added (a namespace the table did not list) | grows `change`, then the new slot and the store word | before its first attach, the store word | UNAVAILABLE while the table read at the current store word omits it, then the generation; inside the agent's rename window too | `per_namespace` (`v`), `adding_namespace` |
| Attached (a first lookup) | nothing | the entry's slot, from the sequence loaded before the table | the entry's generation | `per_namespace` |
| Attach failed (an attached namespace's new generation cannot attach) | as published | the entry's slot, from the sequence it failed at | its last good generation with OK; the handle's other namespaces publish; `detent_refresh` returns the status; it retries when its slot moves or at `detent_refresh` | `entry_failure` |
| First attach failed | as added or published | the store word it failed at | the failure's status until the store word moves or a `detent_refresh` that does not fail | `refresh_memo`, `values` |
| Dropped (the table no longer lists it) | its old slot, then the store word | the store word: its old slot is no longer its own | its last generation with OK, held for the handle's life (follow-up F4) | `per_namespace` (`u` stays at 5) |
| Re-added at a new slot | grows `change`, then the new slot and the store word; never the old slot | the store word until it re-attaches, then the new slot | the re-added generation from the lookup after that publish, then each later publish | `regrow` (listed and unlisted handles, without `detent_refresh`) |
| Re-added with the same generation id at a new slot | as re-added | the new slot: a kept generation watches its entry's slot | the same generation, then the next publish in the new slot | `readd_same_generation` |
| Slot reused by another name | the reused slot and the store word | the new name, its entry's slot; the dropped name, the store word | the new name its generation; the dropped name its last one | `slot_reused` |

## Test plan

Binding tests, both rewritten:

- `bindings/python/tests/test_detent.py` and `bindings/rust/tests/integration.rs` run D4's table (56 rows). It covers flags and integers, including `INT64_MIN` and `INT64_MAX`; typed reads out of range (a flag read of 2, `int8` of `INT64_MAX`, `uint64` of `INT64_MIN`); a missing knob, an invalid name and a namespace outside the handle, each returning the fallback with its status; the hash pairs (`user-1` outside and `user-42` inside a 10% rollout), the switch match, and an integer knob with a rollout and group overrides; and `revision` across two publishes. Python runs the `bool` typed rows only, its ints having no width.
- **Fixture.** Each test makes a fresh store under the build tree: Python under `DETENT_TEST_ROOT`, which the CMake test target sets to `${CMAKE_BINARY_DIR}/.scratch`, and Rust under `CARGO_TARGET_TMPDIR`. Directory trust refuses any group- or world-writable ancestor (`src/detent_local/generation_file_access.hh:103-106`), so `/tmp` and `tempfile`'s default fail. The C++ tests root their stores the same way (`tests/detent_reader/test_reader_core.cc:277`). Python loads the writer through `ctypes`; Rust declares it with `#[link(name = "detent_testing")]` in the test crate, so it never reaches the library.

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Parity | Every row of `scenarios.json` passes in C++, Python and Rust | `ctest -R 'reader_scenarios\|detent_python_test\|detent_rust_test'` |
| T2 Store states | An absent store opens and returns `UNAVAILABLE`, then `OK` once the writer publishes (after the 200 ms retry). A 0777 directory and a uid of `getuid()+1` raise `UNTRUSTED` at open; a store absent at open and created 0777 later answers `UNTRUSTED` from a lookup. A `mapping_bytes` below the minimum raises `INVALID_ARGUMENT`; the minimum opens | Cases ported from `test_reader_core.cc` `opens()` |
| T3 Threads | 8 threads look up `t:rev` while the writer publishes 100 revisions; each thread sees non-decreasing values and only `OK`. Python runs it on the GIL build and on a free-threaded build when CI has one | Python `threading`; Rust `std::thread::scope` |
| T4 Lifetime | Descriptors and the thread count are unchanged after open, lookups and drop or `del`. A forked Python child sees the next publish | `/dev/fd` count, `threading.active_count()` and `os.fork()`; Rust counts `/dev/fd` |
| T5 Mutants | Each fails a test: a binding that ignores the fallback on error; one that drops `hash`; one that maps `UNTRUSTED` to `OK` | Applied one at a time in the developer's ledger |
| T6 CI | The `bindings` job (ubuntu-24.04, macos-15) builds with `DETENT_BUILD_PYTHON=ON DETENT_BUILD_RUST=ON` and runs T1-T4 and the export checks with `--repeat until-fail:3`; both options default `OFF`, so no other job builds them | `ci.yml` `bindings` |

C++ Client tests under H (Q1 (a)):

| Tests | Fate |
| --- | --- |
| `test_file_source`, `test_client_publication_failure`, `test_overrides`, `test_read_view`, `test_client_source`, `test_testing_guard`, `test_concurrency_stress`; `test_eval_semantics` once its cases are rows of `scenarios.json` | Deleted with the code they test. Evaluation stays covered by `test_local_evaluation` and `test_reader_core`, switching by `test_reader_switch` |
| `test_model_and_schema` | Kept; its one `source_poll` mention is a comment (`:152`) |
| `test_lazy_page_access` (`:60-66`), `local_oracle`, `mixed_oracle`, `prepared_client_probe` | Ported from `Client` plus `LocalReadSource` to `detent::Reader` |
| `detent_read_bench` | Replaced by `reader_bench` (reader S6) |

## Limits

1. **Silent:** a misspelled knob returns the fallback in every language unless the caller reads `lookup`'s status (reader.md Limit 4, made universal here by reader Q2 (a)).
2. Python lookups are serialized by the GIL on GIL builds. Only free-threaded builds get the core's parallel lookups.
3. Rust and Python processes load the C++ runtime through `libdetent_reader` (reader.md Limit 8). Wheels and crates.io packaging are out of scope.
4. The store has an entry per namespace (namespace-delivery D8), and the test writer's C face and `Store` write it the same way, but they publish every namespace at each revision, so the scenario table's revisions agree across namespaces; `test_reader_core` moves one namespace alone with a delta.

## Owner questions (ruled)

**Q1. Does step H delete `detent::Client`?** (a) Yes: `detent::Reader` is the one read API in C++, Python and Rust, and `make_local_read_source` goes with the Client (reader.md First steps). (b) Keep `Client` as a C++ wrapper over a `Reader`, a second name for the same thing. (c) Keep `Client` with `read_source` and bind it, which keeps pybind11 and cxx, the defaults and override layers R1 and R2 remove, and the refresh thread, and rewrites both bindings again at S8 and S9. *Recommendation: (a).* Removing the C++ `Client` narrows a documented public API (`website/.../reference/cpp-api/client.md`), so it is the owner's call. The bindings take D1's shape under all three options. **Ruled (a)**, 2026-11-04: "Delete Detent::Client and health() if they're legacy."

**Q2. What replaces `health()`?** Today it returns initialized, connected, accepted version, staleness and knob counts (`detent_python.cc:92-106`, spec section 12). (a) Drop it: `revision(namespace)` and `lookup`'s status are the diagnostics. (b) Add `detent_health(handle, namespace, detent_health *)` to the C ABI, with the last refresh status, the revision and the attached generation's age. A new function needs no ABI version bump. *Recommendation: (a)* for S8 and S9, with (b) as a reader-core addition if operators need staleness. Dropping it removes user-visible behavior, so the call is the owner's. **Ruled (a)**, 2026-11-04: "Delete Detent::Client and health() if they're legacy."

**Ruled (design review F5, F6), 2026-11-23.** D148: `detent get` reads through `detent::Reader`; it takes `--reader-mapping-bytes` and refuses the other nine `--reader-*` capacity flags, `--reader-validation full` and `--defaults` (R1), which stay `detent-agent --status`'s. D149: `libdetent_testing` installs as an opt-in development component (D6 item 2).

## First steps

Q1 and Q2 are ruled. All steps wait on reader S6 reaching main.

| Step | Change |
| --- | --- |
| B1 | D6 items 1-2: the shared `libdetent_reader` with exported C symbols only, the test-only shared `libdetent_testing`, and a link check that the exported set is exactly each library's `exports.list` (`nm -gU` / `nm -D`); the same list writes the version script and the Apple exported-symbols list |
| B2 | D4: `bindings/tests/scenarios.json` and the C++ runner `test_reader_scenarios` |
| B3 | reader S8: the Python C extension (`METH_FASTCALL`, D1-D3), with pybind11 dropped (`bindings/python/CMakeLists.txt:7-11`), and T1-T5 in Python |
| B4 | reader S9: the Rust crate with hand-written `extern "C"`, with `cxx` and `cxx-build` dropped (`bindings/rust/Cargo.toml:12,15`), and T1-T5 in Rust |
| B5 | T6's CI job |
| B6 | H: D6 item 4 and the C++ test table above |

## Review asks

1. D3: no `close()` in Python, against the idiom, to remove the race.
2. D6 item 1: a C-only shared library, or one combined static archive.
