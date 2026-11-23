# legacy-h review: design

Commit: Detent:a5df0985ca338a0c557b5d0e13351685e6f3c97e (main; its code equals Detent:c637e07f, the
quest's start). Design: `docs/design/client-local-source.md` (owner-approved 2026-11-04); step plan
`legacy/wave2-h:docs/status/legacy-wave2.md` Step H (B1-B6); built bindings read at
`quest/detent/legacy-h` Detent:640377d93f51e155c1b3d728c59e4384c9d80b40.
Verdict: CHANGES
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The shape is right: one `Reader` over one C ABI, a C-only shared library, one scenario table with
three runners, the handle closing only with the object, no thread. What is wrong is that the design
and the branch built from it describe the C ABI as it was before one-knob-type (D65, 2026-11-11) and
the D143 mapping cap; reader-s6 landed the newer ABI, so the binding surface, the scenario table and
the status tables must be re-stated before the bindings are rebuilt on it (F1-F4, one text round).

## Evidence I relied on

No job of this quest has run (`campaign status legacy-h`: 0 of 17 criteria with evidence). I relied
on the landed reader core (reader-s6 at Detent:2416ff80, its reviews `reader-s6-correctness.md` and
`reader-s6-security.md`) for the core's own fork, lifetime and status behavior, and did not re-probe
it.

## What I checked myself

Scripts under `.scratch/detent/review-legacy-h-design/` (rerun on a fix):

- `status_drift_probe.py`, job 261123-102412-daf5f7 at the quest head with `MAIN=main`: compares
  the branch's hand-declared status numbers (Rust `from_raw`, Python prelude) with main's
  `reader.h`, and lists the ABI pieces each binding uses that main no longer has. Result (exit 0): 9 of 11 values disagree in both bindings (main 2..9 are INVALID_NAME..IO; the
  branch has WRONG_TYPE at 2 and everything one higher, so main's UNTRUSTED (6) reads as UNAVAILABLE);
  `DETENT_ABI_VERSION` is 1 on main as before; both bindings call `detent_eval`, which main lacks;
  neither passes `mapping_bytes`. (Its `detent_read` line is a false match on `detent_reader`.)
- `export_prefix_probe.sh`, job 261123-102419-ed28cf at the quest head: runs the branch's
  `tests/detent_reader/check_exports.cmake` (`PREFIX=detent`) on a shared library exporting
  `detent_get`, `detent_testing_publish_json` and `detent_internal_helper`, and on a control with a
  C++ symbol. Result (exit 0): the leak library is accepted ("3 exported symbols, all detent_*"); the control
  is refused ("exports __Z10detent_getv").
- Read: the design, reader-core.md, reader.md R1/R2/D8/D9, one-knob-type.md, PLAN D13/D14/D21/D35/
  D65/D143, `include/detent/reader.h` and `reader.hh` at main and at Detent:87687327 (the S6 the
  branch merged), `include/detent/client.hh`, `src/detent_agent/main.cc` (`cmd_managed_get`), the
  branch's `bindings/**`, export lists, check and `bindings` CI job, and a grep of main for every
  caller of a deleted type.

Risk by risk:

1. Capabilities. Every Client capability is either on `Reader` or ruled, with one unruled: the
   application-facing test facility (F6). The rest (`health()`, `Metrics`, `get_or_panic`,
   `get_as`'s `ConversionFailed`, `refresh_alert_after`, `on_update`, `max_knobs`, enumeration via
   `read_view()`/`current_snapshot()`, `detent::global`, overrides, baked defaults, both legacy
   sources) fall under D13, D14, Q2 (a), R1 and R2; F8 asks only that the CHANGELOG name them.
2. Binding contracts. Lifetimes (object is the handle, `&self` borrows, no `close()`), thread safety
   and fork are sound against the landed core: `detent_refresh` returns OK when another thread holds
   `refreshing` (`core.cc:729-730`); a GIL-holding lookup that waits on `refreshing` never waits on a
   thread that needs the GIL, since the core calls back into nothing; os.fork's stop-the-world treats
   a thread inside `refresh()`/open (GIL released) as stopped, and the core's prepare handler waits
   that thread out, which releases `refreshing` before re-attaching. The contracts are unsound only
   against the ABI's shape (F1, F2) and its numbering (F3). A12's three mutants still apply; F1 adds
   the conversion rows they need, F11 the lookup-time UNTRUSTED path.
3. D21. Its premise no longer holds (F5): the identity flags are gone from main and the core has its
   capacity option. Owner call.
4. Export boundary. C++ leakage is enforced (the control is refused); the set of C symbols is not
   (F7). The `bindings` job and the export tests run in every tests build; F9, F10 are CI notes.
5. Contradictions with the landed core: F1, F2, F3, F4.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | The design's binding surface is the pre-D65 ABI: `eval` (bool), `get(name, fallback)` with no hash or switch, `lookup -> Bool\|Int`, `WRONG_TYPE`, `detent_value`/`detent_kind`. Landed: one `detent_get(name, hash, switch_value, int64 fallback)`, no kind, no `WRONG_TYPE`; C++ `get`, `get_as<T>` (fallback when out of T, for bool other than 0/1) and `lookup(name, fallback, ...) -> {status, int64}`. The branch's bindings call `detent_eval`/`detent_read`, and 19 of `scenarios.json`'s 41 rows are `eval`, so A2-A4 and A12 are written against functions that no longer exist. | Design D1 table, D2, test plan ("a wrong type"); `include/detent/reader.h:37-38` at main vs Detent:87687327; `reader.hh:61-80`; PLAN D65; probe 261123-102412-daf5f7; `legacy/wave2-h:bindings/tests/scenarios.json` | Re-state D1/D2/D4 on the landed ABI, mirroring C++: Python `get(name, fallback, hash=None, switch_value=None)` where a `bool` fallback returns a `bool` only for 0 or 1 (C++ `get_as<bool>`), `lookup(name, fallback, hash=None, switch_value=None) -> Lookup(status, int)`; Rust `get(&self, name, fallback: i64, hash, switch_value) -> i64`, `get_as<T: TryFrom<i64>>`, `lookup(..) -> Result<i64, Status>`. Drop the wrong-type rows; add rows for a bool read of a knob holding 2 and a narrow `get_as` out of range; update A2's row count. Test: the rows in `test_reader_scenarios`, `detent_python_test`, `detent_rust_test`. |
| F2 | yes | defect | D1 gives Python and Rust no `mapping_bytes`, the D143 cap C++ `Reader` takes; a Python or Rust reader of a store whose packs exceed 512 MiB answers `NO_MEMORY` fallbacks with no way to raise the cap, which D143 makes per handle ("the agent raises it for 5M"). The branch's Rust `OpenOptions` lacks the field (works only through the size-extension default). | `reader.h:23-31`; `reader.hh:34-44`; PLAN D143; `legacy/wave2-h:bindings/rust/src/lib.rs:19-25` | Add `mapping_bytes` to D1's open row in both bindings (Python keyword `mapping_bytes=0`; Rust an `open` argument or options struct). Test: opening with `mapping_bytes` below `DETENT_MINIMUM_MAPPING_BYTES` raises `INVALID_ARGUMENT` in each language (proves the field reaches the core). |
| F3 | yes | gap | Both bindings hand-number `detent_status` (Rust `from_raw`/`name`, Python's prelude `IntEnum`) and nothing checks them against `reader.h`. reader-s6 renumbered the enum (UNTRUSTED 7 to 6, ...) without moving `DETENT_ABI_VERSION` (1 at both), so D2's "both check `detent_abi_version`" does not catch the drift: the branch's Rust maps main's UNTRUSTED to `Unavailable`. | probe 261123-102412-daf5f7; `legacy/wave2-h:bindings/rust/src/lib.rs` `from_raw`; `legacy/wave2-h:bindings/python/detent_python.cc:25-36`; `reader.h:12-22` | D2 names the check: Python builds `Status` from the header's `DETENT_*` constants compiled into the extension; Rust tests assert, for every value through the last known, `Status::from_raw(raw).name() == detent_status_name(raw)` and each variant's fixed expected name (`Status::Untrusted.name() == "untrusted"`). Test: that case in `detent_rust_test` and a Python one; both fail on the branch's current tables. |
| F4 | yes | gap | The deletion and port table (D6 item 4, quest A6) predates one-knob-type. Unlisted Client callers on main: `tests/detent/test_one_knob_differential.cc` (the oracle: Client::get against main's recorded answers), `tests/detent/one_knob_record.cc`, and the mutant `tests/detent/mutants/c3-bool-accepts-any-nonnegative.patch`, which patches `include/detent/read_view.hh`'s `detail::convert`, a file D6 deletes. Deleting them loses an oracle test (proof standard 3). | `test_one_knob_differential.cc:2,11,56`; `one_knob_record.cc:8`; the patch's header | Add them to D6 item 4 and A6: port the differential to `detent::Reader` over the test writer, and retarget c3 to `reader.hh`'s `get_as<bool>`. Test: the ported `test_one_knob_differential` passes, and fails with the retargeted c3 applied. |
| F5 | no | gap | D21's premise is stale. It keeps `detent get` on `GenerationReader` because "the core lacks generation-identity and capacity open options"; main removed the identity flags (Detent:410242d4, 2026-11-20) and the core has `mapping_bytes` (D143). H's port of `get` to `GenerationReader`/`ReadView` is then a second read path that reader-s7-s9's S7 rewrites. "Loses `--defaults` until S7" also misreads: S7 drops `--defaults` for good (reader.md D8, R1). What still separates `get` from the core: the nine other `--reader-*` capacity flags and `--reader-validation full` (flat or separately trusted stores; step I removes flat). PIC build-wide is unmeasured (F10). | `src/detent_agent/main.cc:219-293` (main); `reader_capacity.hh:7-9`; `docs/USAGE.md:250`; reader.md:211; PLAN D21, D35 | Owner call: (a) keep D21 as is (H ships the GenerationReader port; S7 later); (b) H does S7: `get` opens `detent::Reader` with `--reader-mapping-bytes`, and drops the other `--reader-*` flags and `--reader-validation full` for `get` only (narrows the CLI; `detent-agent --status` keeps them). Recommendation (b): one read path, no throwaway port; reader-s7-s9 keeps S8/S9 reconciliation only. Correct D21's wording to "loses `--defaults` (R1)" either way. |
| F6 | no | gap | Applications lose their test facility with no ruling: `TestKnobs`/`TestGuard` are public and documented (`website/.../reference/cpp-api/global-and-testing.md`); R2 and reader.md D9 make `libdetent_testing` their replacement, but D6 item 2 says it "is never installed". After H a downstream application's tests have no supported way to set a knob. | design D6 item 2; reader.md:21, :215; `include/detent/testing.hh` | Owner call: (a) keep as designed and document that application tests run a store through the agent; (b) install `libdetent_testing` and its header as an opt-in dev component (C++ and the C face for Python/Rust tests); (c) a documented CLI that publishes a JSON knob file into a test store. Recommendation (b): it is what R2 describes. |
| F7 | no | nit | The export check admits any `detent_`-prefixed C symbol: `detent_*` matches `detent_testing_*` and any non-ABI `detent_` helper, so the set of exported functions is not pinned (C++ leakage is refused). | probe 261123-102419-ed28cf; `legacy/wave2-h:tests/detent_reader/check_exports.cmake:17`; `src/detent_reader/exports.map` | List the exact functions in `exports.map`/`.txt` (reader.h's eight; the writer's) and have the check compare sets. |
| F8 | no | nit | The capabilities D14 removes without naming them (Metrics counters, `get_or_panic`, `ConversionFailed`, the staleness alert, `on_update`, enumeration, `max_knobs`) are not listed for users; `Reader::get_as` now answers an out-of-range value with the fallback and status OK. | `client.hh:36-177`; `reader.hh:67-73` | Name each in the CHANGELOG entry (A11) and the website reader page (A14). |
| F9 | no | nit | `Py_MOD_GIL_NOT_USED` is declared with no free-threaded run (quest decision 4 defers it). | design D3; branch `detent_python.cc:390` | A `3.13t` row in the `bindings` job, or drop the declaration until one runs. |
| F10 | no | gap | `CMAKE_POSITION_INDEPENDENT_CODE` build-wide is unmeasured, and the build sets neither hidden visibility nor `-fno-semantic-interposition`, so GCC builds of every static library give up interposable-call inlining. | `legacy/wave2-h:CMakeLists.txt:5`; no visibility flags on main | Limit PIC to the shared twins' dependency closure, or add `-fno-semantic-interposition`, or measure the authority and agent benches on Linux GCC. |
| F11 | no | nit | T2 checks UNTRUSTED only at open; a store absent at open (Q6 (a)) and created untrusted later answers UNTRUSTED from a lookup, the path a binding's status mapping also covers. | reader-core.md "Open and close", refresh step 2 | Add that T2 row so A12's UNTRUSTED mutant dies on the lookup path too. |
| F12 | no | nit | The macOS `bindings` job builds with Apple clang and `-fexperimental-library`; every other macOS job uses Homebrew LLVM (`macos_llvm: true`). | `.github/workflows/ci.yml:21`; branch `ci.yml:60-72` | One macOS toolchain for both. |

## Not run

- No build or test of the branch: it calls functions main no longer has (F1), so its A2-A5 and A12
  evidence from the ledger does not transfer.
- Python's free-threaded lifetime and fork, and Rust fork: reasoned from CPython's call protocol
  and the core's atfork handlers, not run.
- F10's cost: no GCC on this host; not measured.
- The core's own fork, cap and status behavior: reader-s6's reviews, not re-probed.
