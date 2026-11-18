> Status: HISTORY (2026-11-08): superseded by `.campaign/detent/` (`campaign map`); kept for its results and reasoning. Was: (2026-11-03) Ledger for reader steps S1, S2 and S4. Workstream: reader-s1-s4. Design: [reader.md](../design/reader.md) "First steps".

# reader-s1-s4

- Branch / worktree: `phase/reader-s1-s4` at `~/code/Kronuz/Detent-wt/reader-s1`, from `Detent:633f6fb`.
- State: S1, S2 and S4 built and proven locally; CI pending.
- Done:
  - S1 counters behind `DETENT_READ_COUNTERS` (`Detent:3cae505`). Note: `docs/mechanisms/read-counters.md`.
  - S2 `evaluate` lifted out of `ReadView::try_eval` (`Detent:6345d0c`). Note: `docs/mechanisms/evaluation.md`.
  - S4 `GenerationWriter` in C++ and C (`Detent:632bd3a`). Note: `docs/mechanisms/generation-writer.md`.
- Proof (local, Apple clang, Release unless noted):
  - S1 values: `test_local_read_counters` pins the first `a:k` lookup at 2 segment opens, 5 pages verified and 1466 bytes hashed (derived by hand from the page sizes, in the test), a warm repeat at zero, a `b:k` lookup at 1, 2 and 338, and an open at 1 directory walk, 5 record opens and the exact record bytes.
  - S1 mutants, both killed: dropping the `pages_verified` count; `table_bytes` off by one.
  - S1 costs nothing off: the object code (`otool -tvV`) of `lazy_page_access`, `prepared_reader`, `reader_metadata`, `ownership` and `paged_layout` with the flag off is identical to the same files at `Detent:633f6fb` (0 differing lines each).
  - S2: `test_local_evaluation` pins values and matches `detail::evaluate` on 4,680 combinations. Mutants killed: `<` to `<=` in the bucket test; global rollout checked before the switch. `view.o` is not byte-identical (269 differing normalized lines: register and stack layout after the inline lift), so a performance claim needs the lead's measurement.
  - S4 mutants, both killed: revision not advanced; bool default inverted. The writer test reads values back through `PreparedReader` across 6 revisions, a reopened store and the C face (including a malformed-JSON error).
  - Sanitizers, separate builds with counters on (Debug, `-O1`): ASan+UBSan and TSan run `test_local_{read_counters,evaluation,view,lazy_page_access,reader}` and `test_testing_generation_writer`; all pass with no sanitizer report. The writer and counter tests are single-threaded, so TSan shows no race in the counters' use, not a concurrency proof.
  - Fuzz: none added; no decoder or parser was added or changed (the writer feeds the existing JSON parser and snapshot decoder).
- Design vs code:
  - D6 says `evaluate(def, qualified, hash, switch)`; the code takes `ns` and `key`, because the flat layout stores its own qualified name and the name is built only when a ramp needs it. Behavior is the lifted code's.
  - S1 lists "table bytes read"; there is no table yet, so it counts bytes read from generation records.
  - D9 says each call moves the namespace sequence and the store word; the `change` file is S5, so the writer moves only the selected generation.
  - D9 says the C function takes the JSON "the authority already parses"; it takes the defaults-file JSON (`parse_defaults`).
- Next: lead's review; S5 adds the sequences to the writer; S6 consumes the counters in T1 and T2.
- Blocked on: nothing.
