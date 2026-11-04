> Status: DRAFT (2026-11-04) Designing, ready for preliminary review. Refines [reader.md](reader.md) D7, S8 and S9 for legacy wave 2 step H; changes Step H option B in `docs/status/legacy-wave2.md` (branch `legacy/wave2-f`).

# Client local source in the bindings

Python and Rust read managed generations through the reader core, the same `detent::Reader` C++ uses, and step H deletes the Client that `make_local_read_source` plugs into.

- Author: [Germán Méndez Bravo](mailto:german.mb@gmail.com). Created 2026-11-04. Ledger: `docs/status/client-local-source.md`.
- Base: `Detent:3e8bdbc` (main). Reader core citations are at `reader/s6`, `Detent:70f648f`; Step H at `legacy/wave2-f`, `Detent:d18d6f5`.
- Depends on: reader.md R1 (defaults per call), R2 (no overrides), R4 (explicit handles), D1 (the C ABI), D9 (the test writer), and the lead's (a) calls on its Q2 and Q6. Needs reader S6 on main.

## Abstract

The owner ruled today to drop the unmanaged agent and remove the Client's legacy sources: `source_path`, which polls a snapshot file, and `source_poll`, which takes a user function returning snapshots or patches (`include/detent/client.hh:62,99-101`). Both bindings build a `detent::Client` from a defaults file, an optional `source_path`, CLI overrides and a poll interval (`bindings/python/detent_python.cc:48-70`, `bindings/rust/src/shim.cc:25-38`). After H the only source left is `read_source`, filled by `make_local_read_source` (`include/detent_local/client_source.hh:31`).

**Recommendation: bind the reader core, not the Client.** R1 removes the Client's baked defaults and R2 its CLI overrides; what remains over a local source is a refresh thread (`src/detent_local/client_source.cc:113-128`) that the core replaces with one load per lookup. reader.md already moves Python and Rust onto the C ABI (S8, S9) and deletes `Client` in H (`reader.md:312,323-324`), so binding `make_local_read_source` now writes each binding twice. This design fixes the shared shape, mirroring `detent::Reader` (`include/detent/reader.hh:29-90` at `reader/s6`), and lists what H deletes. Nothing here is measured; speed is reader.md T1's row, which the lead measures.

## Decisions

### D1. One type: `Reader`, in all three languages

| | C++ (`reader.hh`) | Python (`detent`) | Rust (`detent`) |
| --- | --- | --- | --- |
| Open | `Reader(store, publisher_uid, namespaces = {})`, throws `ReaderError` | `Reader(store, publisher_uid, namespaces=())`, raises `ReaderError` | `Reader::open(store, publisher_uid, &[..]) -> Result<Reader, ReaderError>` |
| Bool | `eval(name, fallback, hash, switch_value) -> bool` | `eval(name, fallback, hash=None, switch_value=None) -> bool` | `eval(&self, name, fallback, hash: Option<&str>, switch_value: Option<&str>) -> bool` |
| Integer | `get(name, fallback) -> int64_t` | `get(name, fallback) -> int` | `get(&self, name, fallback) -> i64` |
| Status | `lookup(name, hash, switch_value) -> Lookup{status, value}` | `lookup(...) -> Lookup` (`.status`, `.value`, truthy on `OK`) | `lookup(...) -> Result<Value, Status>`, `enum Value { Bool(bool), Int(i64) }` |
| Refresh | `refresh() -> detent_status` | `refresh() -> Status` | `refresh(&self) -> Result<(), Status>` |
| Revision | `revision(ns) -> optional<uint64_t>` | `revision(namespace) -> int \| None` | `revision(&self, ns) -> Option<u64>` |
| Close | destructor | deallocation | `Drop` |

The arguments are the C ABI's `detent_open_options` (`include/detent/reader.h:25-30`):

- `store` is the agent's store directory.
- `publisher_uid` is required, because trust is never inferred from the directory owner (reader.md D1 item 5). An application passes the uid its agent runs as.
- `namespaces` limits the handle to those namespaces. Empty means all of them, including ones published later.

There is no defaults path (R1), no override list (R2) and no poll interval: a lookup sees a publish through the mapped `change` file (reader.md D4), and `refresh()` pays the attach earlier from a thread the caller chooses.

Differences, each forced by the language:

1. Rust's fallible constructor is `open` returning `Result`, after `detent_open` and `File::open`.
2. Rust `lookup` is `Result<Value, Status>`: a sum type carries what C++ spreads over `kind`, `boolean` and `integer`. Python's `Lookup.value` is a `bool` or an `int`, so its type carries the kind.
3. `store` takes `os.PathLike | str | bytes` in Python and `impl AsRef<Path>` in Rust, passed as raw bytes; a `&str` would refuse non-UTF-8 paths the C ABI accepts.
4. Python checks types first: a non-`bool` fallback to `eval`, a non-`int` or `bool` fallback to `get`, or a non-`str` name raises `TypeError`; an integer outside int64 raises `OverflowError`. C++ and Rust reject these at compile time.
5. Only C++ keeps `native()`, for C callers; the bindings have none.

### D2. Errors: open raises, lookups never do

Only opening fails loudly: `ReaderError` carries the `detent_status`, with the message `detent_open: <detent_status_name>` as in C++ (`reader.hh:42-44`). Under reader Q6 (a), opening a store that does not exist yet succeeds, and lookups return the fallback with `UNAVAILABLE` until a generation appears (`src/detent_reader/core.cc:614-624`). Every lookup returns the caller's fallback on any error, and `lookup` reports the status (reader Q2 (a), the lead's call; Python's raising today is the owner's to keep or drop). `Status` is a Python `IntEnum` and a Rust `#[non_exhaustive]` enum with `Unknown(u32)`. The C ABI may add statuses without a version bump ("known or not, is an error", `reader.h:23`), so Rust declares the C return type as `c_int` and maps it: reading an out-of-range value into a Rust enum is undefined behavior. Python maps an unknown value to a pseudo-member that keeps the integer. Both check `detent_abi_version` once: Python at import, raising `ImportError`; Rust in `open`, as C++ does in its constructor.

### D3. Lifetime: the object is the handle, with no `close()`

A close that races a lookup is undefined (reader.md D1 item 7), so in every language the handle closes when the object ends and never earlier. Rust lookups borrow `&self`, so `Drop` cannot overlap one; `Reader` is `Send + Sync`, not `Clone`, shared through `Arc`. In Python every lookup holds a reference to the reader, so deallocation cannot overlap one either; a `close()` or context manager would bring the race back, so there is none.

Python opens in `tp_new`, with no `__init__` and no subclassing, so the handle never changes after construction. Lookups keep the GIL (reader.md D7); open and `refresh()` release it around their I/O. The extension declares `Py_MOD_GIL_NOT_USED`: the core's lookups are thread-safe and the object is immutable, so free-threaded builds need no lock. A forked child keeps a working handle (reader.md D10). No language starts a thread.

### D4. One scenario table, three runners

`bindings/tests/scenarios.json` lists each case (operation, arguments, expected value and status), and a C++ runner over `detent::Reader`, the Python tests and the Rust tests all read it, so a language that drifts fails. Each publishes the same knobs through the test generation writer, which writes real generations into a store (reader.md D9, on main; C face `include/detent_testing/generation_writer.h:14-19`).

### D5. Old call to new call

| Old (binding or C++) | New | Why |
| --- | --- | --- |
| `detent.Client(defaults_path)`; `Client::new(defaults, None, vec![], ms)` | `detent.Reader(store, uid)`; `Reader::open(store, uid, &[])` | R1: defaults move into each call |
| `Client(defaults, source_path=p)`; `Client::new(d, Some(p), ..)` | Same `Reader`, over the managed agent's store | The file source is removed (owner, 2026-11-04) |
| `cli_overrides=[...]` | None. Tests publish values with the writer | R2 |
| `poll_interval_ms=N` | None; `refresh()` to attach earlier | reader.md D4, Q1 (a) |
| `eval(name, hash_val=, switch_val=)` | `eval(name, fallback, hash=, switch_value=)` | R1 |
| `get(name, switch_val=)` | `get(name, fallback)` | The spec ignores switch for integers (reader.md D1 item 3) |
| `except DetentError` around `eval`/`get`; Rust `?` | `lookup(name).status`; Rust `lookup(..)?` | reader Q2 (a) |
| `except DetentError` around construction | `except ReaderError as e: e.status` | D2 |
| `health()` | `revision(namespace)`; the rest is Q2 below | No core equivalent |
| C++ `InitOptions{.read_source = make_local_read_source(opts)}`, `make_managed_read_source(dir, uid)` | `detent::Reader(dir, uid, namespaces)` | Q1 |
| C++ `auto_refresh = false` with `LocalReadSource::refresh()` | `Reader::refresh()`; there is never a thread | reader.md D5 |

### D6. What the C++ side must provide

1. **`libdetent_reader` as a shared library exporting only `detent_*`**, every C++ symbol hidden (a version script on Linux, an exported-symbols list on macOS), its C++ dependencies linked in. Today it is `add_library(detent_reader core.cc)` with `detent::local_view` PRIVATE (`src/detent_reader/CMakeLists.txt:3-6`), static by default; cargo cannot follow CMake's link interface, which is why `build.rs` hand-lists eight include directories and three archives (`bindings/rust/build.rs:26-48`). With the shared library `build.rs` is a link search, `dylib=detent_reader` and an rpath, and cargo compiles no C++. The Python extension links the same file.
2. **`libdetent_testing` as a shared, test-only library** under `DETENT_BUILD_TESTS`, exporting only `detent_testing_*`. It is never installed. Today it is a default-static library (`src/detent_testing/CMakeLists.txt:4`).
3. **Nothing new in the ABI** for the surface above. A health call is Q2.
4. **Step H deletes** `Client`, `InitOptions` and the source plumbing behind it (`ReadSource`, `ReadSourceFactory`, `ClientReadView`, `EvaluationView`), `LocalReadSource`, `ClientSourceOptions`, both factories, `OverrideTable`, `detent::global`, `TestKnobs` and `Client(Snapshot)`, provided Q1 is (a).

## Test plan

Binding tests, both rewritten:

- `bindings/python/tests/test_detent.py` and `bindings/rust/tests/integration.rs` run D4's table. It covers bool and integer values, including `INT64_MIN` and `INT64_MAX`; a missing knob, a wrong type, an invalid name and a namespace outside the handle, each returning the fallback with its status; today's hash pairs (`user-1` false and `user-42` true at a 10% rollout, `test_detent.py:75-76`) and the switch match; and `revision` across two publishes.
- **Fixture.** Each test makes a fresh store under the build tree: Python under `DETENT_TEST_ROOT`, which the CMake test target sets to `${CMAKE_BINARY_DIR}/.scratch`, and Rust under `CARGO_TARGET_TMPDIR`. Directory trust refuses any group- or world-writable ancestor (`src/detent_local/generation_file_access.hh:103-106`), so `/tmp` and `tempfile`'s default fail. The C++ tests root their stores the same way (`tests/detent_reader/test_reader_core.cc:277`). Python loads the writer through `ctypes`; Rust declares it with `#[link(name = "detent_testing")]` in the test crate, so it never reaches the library.

| Row | Target | Procedure |
| --- | --- | --- |
| T1 Parity | Every row of `scenarios.json` passes in C++, Python and Rust | `ctest -R 'reader_scenarios\|detent_python_test\|detent_rust_test'` |
| T2 Store states | An absent store opens and returns `UNAVAILABLE`, then `OK` once the writer publishes (after the 200 ms retry). A 0777 directory and a uid of `getuid()+1` raise `UNTRUSTED` | Cases ported from `test_reader_core.cc` `opens()` |
| T3 Threads | 8 threads look up `t:rev` while the writer publishes 100 revisions; each thread sees non-decreasing values and only `OK`. Python runs it on the GIL build and on a free-threaded build when CI has one | Python `threading`; Rust `std::thread::scope` |
| T4 Lifetime | Descriptors and the thread count are unchanged after open, lookups and drop or `del`. A forked Python child sees the next publish | `/dev/fd` count, `threading.active_count()` and `os.fork()`; Rust counts `/dev/fd` |
| T5 Mutants | Each fails a test: a binding that ignores the fallback on error; one that drops `hash`; one that maps `UNTRUSTED` to `OK` | Applied one at a time in the developer's ledger |
| T6 CI | A new `bindings` job (Linux x86_64, macOS arm64) builds with `DETENT_BUILD_PYTHON=ON DETENT_BUILD_RUST=ON` and runs T1-T4. No workflow builds the bindings today: `ci.yml` has no Python binding, `cargo` or `pybind` step, and both options default `OFF` (`CMakeLists.txt:47-48`) | Run on the branch first, with `--repeat until-fail:5` |

C++ Client tests under H, if Q1 is (a):

| Tests | Fate |
| --- | --- |
| `test_file_source`, `test_client_publication_failure`, `test_overrides`, `test_read_view`, `test_client_source` | Deleted with the code they test. Evaluation stays covered by `test_local_evaluation` and `test_reader_core`, switching by `test_reader_switch` |
| `test_model_and_schema` | Kept; its one `source_poll` mention is a comment (`:152`) |
| `test_lazy_page_access` (`:60-66`), `local_oracle`, `mixed_oracle`, `prepared_client_probe` | Ported from `Client` plus `LocalReadSource` to `detent::Reader` |
| `detent_read_bench` | Replaced by `reader_bench` (reader S6) |

## Limits

1. **Silent:** a misspelled knob returns the fallback in every language unless the caller reads `lookup`'s status (reader.md Limit 4, made universal here by Q2 (a)).
2. Python lookups are serialized by the GIL on GIL builds. Only free-threaded builds get the core's parallel lookups.
3. Rust and Python processes load the C++ runtime through `libdetent_reader` (reader.md Limit 8). Wheels and crates.io packaging are out of scope.
4. Until namespace delivery's table lands, the store has one table entry, slot 0 in `change` (`docs/status/reader-s5-s6.md`, "Design vs code"), so T1 cannot see per-namespace revisions differ.

## Open questions for the owner

**Q1. Does step H delete `detent::Client`?** (a) Yes: `detent::Reader` is the one read API in C++, Python and Rust, and `make_local_read_source` goes with the Client (reader.md First steps). (b) Keep `Client` as a C++ wrapper over a `Reader`, a second name for the same thing. (c) Keep `Client` with `read_source` and bind it, which keeps pybind11 and cxx, the defaults and override layers R1 and R2 remove, and the refresh thread, and rewrites both bindings again at S8 and S9. *Recommendation: (a).* Removing the C++ `Client` narrows a documented public API (`website/.../reference/cpp-api/client.md`), so it is the owner's call. The bindings take D1's shape under all three options.

**Q2. What replaces `health()`?** Today it returns initialized, connected, accepted version, staleness and knob counts (`detent_python.cc:92-106`, spec section 12). (a) Drop it: `revision(namespace)` and `lookup`'s status are the diagnostics. (b) Add `detent_health(handle, namespace, detent_health *)` to the C ABI, with the last refresh status, the revision and the attached generation's age. A new function needs no ABI version bump. *Recommendation: (a)* for S8 and S9, with (b) as a reader-core addition if operators need staleness. Dropping it removes user-visible behavior, so the call is the owner's.

## First steps

None of these waits on Q1 or Q2. All of them wait on reader S6 reaching main.

| Step | Change |
| --- | --- |
| B1 | D6 items 1-2: the shared `libdetent_reader` with exported C symbols only, the test-only shared `libdetent_testing`, and a link check that only `detent_*` is exported (`nm -gU` / `nm -D`) |
| B2 | D4: `bindings/tests/scenarios.json` and the C++ runner `test_reader_scenarios` |
| B3 | reader S8: the Python C extension (`METH_FASTCALL`, D1-D3), with pybind11 dropped (`bindings/python/CMakeLists.txt:7-11`), and T1-T5 in Python |
| B4 | reader S9: the Rust crate with hand-written `extern "C"`, with `cxx` and `cxx-build` dropped (`bindings/rust/Cargo.toml:12,15`), and T1-T5 in Rust |
| B5 | T6's CI job |
| B6 | H, once Q1 is answered: D6 item 4 and the C++ test table above |

## Review asks

1. D3: no `close()` in Python, against the idiom, to remove the race.
2. D6 item 1: a C-only shared library, or one combined static archive.
3. Q1: reader.md deletes `Client` in H; Step H option B keeps it. Which governs.
