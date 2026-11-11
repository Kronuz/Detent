# one-knob-type: journal

State: blocked on lead: file set (B1) and out-of-bucket value for default-true bools (B2)
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Survey done; no code changed yet. Both blockers below gate the code: B1 because the tree does
not compile with only the set's files moved, B2 because it decides `evaluate` and A1's oracle.

## Next

1. On B1/B2 rulings: model (`include/detent/model.hh`): one `KnobDef {int64 default_value;
   optional<uint8_t> rollout_pct; int64 rollout_value; flat_map<string, GroupOverride> overrides}`,
   `GroupOverride = variant<int64_t, GroupRollout>`; `OverrideValue = int64_t`.
2. Evaluation: one `evaluate -> int64` in `detail/evaluation.hh` and `src/detent_local/evaluation.hh`;
   `Client::get(name, hash?, group?)`, `get_as<T>` (bool: 0/1 else ConversionFailed); `eval` gone.
3. JSON (`json_schema.cc`, `json_patch.cc`): no `type`; validation refuses `rollout_value`
   without any percentage, a percentage without `rollout_value`, pct > 100.
4. Wire (`detent_distribution` protocol/encoders, `WireBoolDef` -> one `WireDefinition`),
   pages (`paged_definition`: no type byte), authority state, CLI, bindings (Python, Rust).
5. A1: frozen copy of main's `detail::evaluate(BoolDef)` as oracle in a suite test over the
   existing scenarios' small domain (`tests/detent/test_one_knob_differential.cc`).
6. A2 tests; A4 grep job; A3 fixed vs varint numbers (page bytes/knob at 100K and 5M, read
   bench and first read, exclusive interleaved pairs per D51); A5 docs.

## Blocked on

**B1 (lead): file set.** "Every consumer in this repository moves in this quest", but the set
misses most consumers. Files that reference `BoolDef`/`IntDef`/`eval`/the type field, outside the
set (from `git grep`, kept in `.scratch/touch-candidates.txt`):

- `src/detent_local/`: evaluation.hh, borrowed_definition.{hh,cc}, borrowed_snapshot_source.cc,
  builder.cc, applied_source.hh, applied_base_source.hh, flat_state_rows.hh,
  paged_delta_leaves.cc, paged_snapshot_builder.cc, snapshot_source.cc, state_cursor.{hh,cc},
  state_equivalence.cc; `include/detent_local/view.hh`
- `src/detent_authority/`: change_history.cc, export_encoder.cc, read_reply.cc, wire_fields.hh
- tests: `tests/detent_local/**` (about 20 files incl. test_evaluation.cc),
  `tests/detent_authority/**` (about 25), `tests/detent_system/**`, `tests/detent_testing/**`,
  `tests/detent_delivery/**`, `tests/detent_agent/test_agent_cli.cmake`,
  `tests/cmake_consumer/main.cc`
- `benchmarks/*.cc` (read, scale, update, authority, codec, command_work, read_response,
  terminal_retirement, btree-repair, container-alternative), `tools/w5/**`, `examples/demo.cc`
- user docs: `website/src/content/docs/**` (knobs, rollouts, group-overrides, snapshot-format,
  cpp-api, quickstart, guides), `docs/design/reader.md`, `docs/AUTHORITY_ARCHITECTURE.md`,
  `docs/LOCAL_IMAGE_FORMAT.md`
- Left alone as history (not changed): docs/reviews, docs/status, docs/handoffs, AUDIT.md,
  benchmarks/*.json, *.patch mutants of landed quests.

Ask: extend the set with `src/detent_local/**`, `include/detent_local/view.hh`,
`src/detent_authority/**`, `tests/**`, `benchmarks/**`, `tools/w5/**`, `examples/**`,
`website/src/content/docs/**`, `docs/design/reader.md`, `docs/AUTHORITY_ARCHITECTURE.md`,
`docs/LOCAL_IMAGE_FORMAT.md`, `docs/perf/one-knob-type*` (A3's procedure and numbers).
CHANGELOG.md stays the lead's: the entry is "one knob type: `get`/`get_as`, `eval` removed, JSON
`type` removed, `rollout_value` added, page and wire formats changed".

**B2 (lead, possibly owner): what a hashed caller outside the bucket gets.** Today a bool's
percentage (knob-wide or group) returns `bucket < pct` when a hash is given, so a hashed caller
outside the bucket gets `false` whatever the default; the default is only the no-hash answer.
With `default` and `rollout_value` alone, a former bool with `default: true` and a percentage
cannot be expressed exactly. The existing tests cover that case, so A1 as written requires it:
`tests/detent/test_eval_semantics.cc` main() (`default true, rollout 100, group "none" pct 0`:
hash + "none" -> false) and `tests/detent_local/test_evaluation.cc` (`make(true, 0)` with hash ->
false; `make(true, {}, {g: pct 0})` -> false; the oracle sweep over `fallback: true`).

1. *Outside the bucket gets the default* (recommended). The one natural int reading: a rollout
   ramps from `default` to `rollout_value`, and a bool is `default 0, rollout_value 1`. Exact for
   every former bool with `default: false`, and for `default: true` without a percentage or with
   only forced groups. Changes `default: true` plus a percentage: hashed callers outside the
   bucket get 1 where they got false (callers on today stay on, as the quest's ramping sentence
   says). A1's differential then excludes exactly those cells, enumerated and asserted to differ,
   and the two test lines above change with it.
2. *An explicit off value* (`rollout_off`, defaulting to `default` in JSON, one more int64 in a
   ramped definition's page and wire encoding). Exact for every former bool; costs a fourth field
   and a second way to say "the default".
3. *Outside the bucket gets 0.* Exact for bools, wrong for ints (default 25, rollout 10% to 50
   gives 90% of hashed callers 0). Not recommended.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

Not yet.

## Rulings I made

- R1 JSON: no `type`; `rollout_value` is required when the definition has any percentage
  (knob-wide or group) and refused otherwise (explicit; A2's "rollout value without a rollout").
- R2 `get_as<bool>`: 0 is false, 1 is true, anything else ConversionFailed (never truncated).
- R3 CLI overrides: one `int64`; `true`/`false` stay accepted as 1/0.
