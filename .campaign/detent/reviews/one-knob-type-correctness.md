# one-knob-type review: correctness

Commit: Detent:b1beff41ba60
Verdict: APPROVE
Date: 2026-11-11

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Suite 261111-225634-f03976 (277 tests, passed at Detent:d2154c22). Detent:d2154c22..b1beff41
  changes only the journal and docs/design/observability.md, so it stands for this commit. It
  runs test_one_knob_differential (A1, main's recorded answers), test_local_evaluation (paged
  evaluator against `detail::evaluate` over the full small domain, D68 cells included),
  test_local_paged_definition, test_eval_semantics (`get_as<bool>` on 2 and -1 through Client)
  and test_testing_guard (global adapter on 2). tests/CMakeLists.txt:2 builds them with
  `-UNDEBUG`, so their asserts run.
- A4 261111-201651-a2bd67 at Detent:252ec99d: builds the Python and Rust bindings and runs their
  suites.
- I did not rebuild or rerun anything those jobs show.

## What I checked myself

Probe job 261111-233447-a191e3 (exit 0, at Detent:b1beff41), from
`.scratch/review-one-knob-type-correctness/run.sh` with `probe_paged.cc`, `probe_flat.cc` and
`probe_get_as.cc`. To rerun:
`campaign job submit --quest one-knob-type --level light --minutes 25 -- bash .scratch/review-one-knob-type-correctness/run.sh`.

1. D68 in both evaluators. `detail::ramp()` (include/detent/detail/evaluation.hh:10) returns the
   fallback when there is no hash, and otherwise `stable_bucket < pct ? rollout_value : fallback`.
   Both evaluators call it with the knob's default as the fallback:
   include/detent/detail/evaluation.hh:21 and src/detent_local/evaluation.hh:28. No other
   evaluator exists: `stable_bucket` appears only in evaluation.hh and hashing.cc, and
   `evaluate(` has callers only in client.cc, read_view.cc and view.cc. When a group override
   matches and the caller has no hash, a group percentage returns the default and does not fall
   through to the knob-wide rollout, as on main; a forced value wins with or without a hash. The
   suite covers the paged body. The probe adds the flat image, which no suite test covers with
   default 1. For default 1, rollout value 7 and 50%, the flat path returned 7 for 208 hashes and
   1 for 192, the same as `detail::evaluate` for every hash; without a hash it returned 1. A
   group percentage with no knob-wide rollout also matched the model.
2. Paged decoder on untrusted bytes. `get_varint` uses `varint::leb128::decode` at the pinned
   Detent dependency `varint:333384ee` (cmake/dependencies.json:155). That decoder refuses a
   trailing zero byte (overlong), a 10th byte above 1 and an 11th byte. `read_override` and the
   header bound every read by the body's end, and the override count is capped at
   `maximum_overrides` and below 2^14. The probe sends crafted rows to `validate_definition`:
   - Refused: an overlong forced value with a consistent row length (0x80 0x00), a value with
     bits past 64, a value that runs into the key, a count past the rows, count 256 with no rows,
     count 257, and a rollout value with only forced rows.
   - Accepted: a full 64-bit value (INT64_MIN).

   It also ran 400,000 random mutations of four valid bodies. Of the 29,114 that
   `validate_definition` accepted, every one re-encoded byte for byte, had a legal shape, and
   evaluated the same through `BorrowedDefinition` and through `detail::evaluate`. No other
   exception types were thrown.
3. A definition with a value and no percentage, or the reverse, cannot reach storage. Every
   ingress checks `definition_error` (include/detent/model.hh:38):
   - JSON: src/detent/json_schema.cc:143.
   - Wire: src/detent_distribution/protocol.cc:116 (`finish`).
   - Authority state, encode and decode: src/detent_authority/state.cc:107, :180 and :560.
   - Paged encoder: src/detent_local/paged_definition.cc:37.

   The paged decoder (paged_definition.cc:146) and the flat validator
   (src/detent_local/view.cc:237 and :257) refuse the shape on read. The probe confirms the flat
   validator refuses all four combinations of value and percentage, and refuses an unknown flag.
   CLI overrides (src/detent/overrides.cc) produce plain int64 values in `OverrideTable` and never
   build a definition. src/detent_cli constructs no `KnobDef`.
4. `get_as<bool>` uses a single conversion, `detail::convert` (include/detent/read_view.hh:17),
   which accepts only 0 and 1. It has these callers:
   - `Client::get_as` at include/detent/client.hh:150.
   - `ClientReadView::get_as` at include/detent/read_view.hh:52.
   - The global adapter (global.hh:40), which delegates to `Client::get_as`.
   - Python `get_bool` (bindings/python/detent_python.cc:85).
   - Rust `get_bool` (bindings/rust/src/shim.cc:57).

   The probe refused 2, -1 and INT64_MIN on Client, ClientReadView and the global adapter. It
   also refused a 100% ramp that serves 2, and `get_as<uint8_t>` on -1.
5. R14 does not hide a refusal. `PagedPacker::commit` (src/detent_local/paged_packer.cc:586) and
   `TrieCursor::commit` (src/detent_local/paged_trie_cursor.cc:175) return false only from
   `grant` and `admit`, which are pure budget checks (rows or bytes exhausted). Every validation
   failure throws and marks the object failed. The fixture's retry loop
   (tests/detent_local/dense_graph_fixture.hh:88-101) refreshes the budget at most 16 times, so a
   thrown refusal still propagates.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no | gap | The "non-canonical varint" and "varint past 64 bits" cases in tests/detent_local/test_paged_definition.cc:97-103 put the bad varint in the header, where `validate_definition` would also refuse it for its length, because `offset` starts at `header_size(result)` and must end at `bytes.size()`. They would pass even with a decoder that accepts overlong encodings. The refusal that matters is in override rows, where `read_override` advances by the bytes it consumed. The pinned varint library refuses it there (probe), but no Detent test shows it. | tests/detent_local/test_paged_definition.cc:97-103; probe_paged.cc first block | Add the overlong forced-row case (`{0, g->0}` with byte 14 set to 0x80 and 0x00 inserted after it) to test_paged_definition. |
| C2 | no | gap | No suite test evaluates a flat-image record with default != 0 under a percentage. The D68 cell and the read of the rollout value at offset 48 (borrowed_definition.cc:30) are unproven in the suite. test_view.cc covers only default 0 and rollout value 1. The probe shows they are correct. | tests/detent_local/test_view.cc:127-131; probe_flat.cc | Add a default-1, rollout-value-7 record to test_view's image, or move probe_flat.cc into the suite. |
| C3 | no | gap | The Python and Rust binding tests check `get_bool` only on 25, not on 2 or -1. Both call `Client::get_as<bool>`, so the probe's Client result covers them by construction. | bindings/python/tests/test_detent.py:64; bindings/rust/tests/integration.rs:61 | Optional: one -1 case in each binding test. |
| C4 | no | nit | `LeafBody` checks the header with `encode_definition_header`, which refuses a percentage without a value but not a value without any percentage. It never sees the group rows together with the header. Every source feeding it is already shape-validated, and readers refuse such a body, so nothing reaches a page today. | src/detent_local/paged_definition.cc:53-57; src/detent_local/paged_leaf_body.cc:70-95 | Optional: at the end of `Phase::Overrides`, require that the body has some percentage whenever it has a rollout value. |
| C5 | no | nit | The authority export encoder still names the definition `flag_`, a two-type word. | src/detent_authority/export_encoder.cc:36-38 | Rename to `definition_`. |

## Not run

- Sanitizers, Linux and FreeBSD (CI's).
- A3 timing, which is outside this lens.
- I did not rebuild the bindings (A4's job did).
- I did not fuzz the wire decoder or the authority state decoder beyond reading their shape checks.
- There is no Debug-build run. The probes use their own checks, which do not depend on asserts.
