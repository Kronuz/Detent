# One knob type

A knob is one signed 64-bit integer definition in every layer: the model, JSON, the authority's
state, the distribution wire, paged bodies, the flat image, the CLI and the bindings. A flag is
`default 0, rollout_value 1`. The semantics are in [knob-model.md](knob-model.md); this note gives
the encodings, the out-of-bucket rule, and the measurements behind the paged body (A3).

## Definition

```text
KnobDef { int64 default_value; flat_map<string, int64 | GroupRollout{pct}> overrides;
          optional<uint8> rollout_pct; optional<int64> rollout_value }
```

`definition_error()` (`include/detent/model.hh`) is the one shape check: every percentage is at
most 100, and `rollout_value` is present exactly when some percentage exists, knob-wide or in a
group. JSON parsing, authority publish and decode, the wire decoder, paged and flat validation all
refuse a definition it rejects. A percentage with no value would have nothing to serve; a value
with no percentage would be a second, unreachable default.

## Outside the bucket (D68)

A hashed caller outside a percentage's bucket gets `default`, as does a caller with no hash. A
rollout ramps a knob from `default` to `rollout_value`; for a flag that is off to on. This differs
from the two-type model in one cell: a flag whose default is 1 with a percentage, where a hashed
caller outside the bucket got `false` and now gets 1. `tests/detent/test_one_knob_differential.cc`
enumerates those cells against a frozen copy of the two-type evaluator and asserts exactly that
difference; every other cell is equal. The rule lives in `detent::detail::ramp()`, which every
evaluator calls.

## Encodings

| Layer | Definition |
| --- | --- |
| JSON | `{"default": d, "overrides": {...}, "rollout_pct": p, "rollout_value": v}`; absent fields omitted, `null` accepted for the two optional ones |
| Wire (MessagePack) | `[default, pct or nil, value or nil, [rows]]`; a row is `[key, 0, int64]` or `[key, 1, pct]` |
| Authority charge | `9 + (pct ? 1) + (value ? 8) + (overrides ? 8)` per definition, `8 + key + 1 + (forced ? 8 : 1)` per override, against `definition_bytes` |
| Paged body | below |
| Flat image | 56-byte record: default @24, override start @32, count @40, flags @44 (bit 0 pct, bit 1 value), pct @45, zero @46..48, value @48; 24-byte override row: kind @12, zero @13..16, value @16 |
| Reader metadata (DTREAD01) | one records count @80; @84 reserved, zero |

### Paged body

Big-endian revision, then canonical LEB128 (signed values zigzagged):

```text
header   revision u64 | pct u8 (255 none) | flags u8 (bit 0: value) | default | count | [value]
override len u8 | kind u8 (0 forced, 1 pct) | value (forced: varint, pct: u8) | key
```

A flag with a rollout and a one-byte default is 13 bytes; an integer with a small default is 12.
Validation checks the flags, the percentages, that every varint is canonical (no trailing zero
group, nothing past 64 bits) so `header_size()` recomputes the encoded length, strictly ascending
UTF-8 keys, that `count` rows fill the body exactly, and `definition_error`'s rule on the decoded
fields. Strings accounting counts the validated key bytes (`Definition::key_bytes`), since row
sizes vary. `BorrowedDefinition::from_paged` decodes the header once per read.

## Fixed width or varint (A3)

Two candidates with the same fields: the varint body above, and a fixed-width one (revision u64,
default i64, pct u8, flags u8, count u16, value i64; rows len u8, kind u8, value i64, key) at
Detent:7d2c82fc. `docs/perf/one-knob-type-corpus.py` converts main's corpora to either.
Procedure and raw numbers: [../perf/one-knob-type.md](../perf/one-knob-type.md).

The 100K corpus has 52,022 integer and 47,978 flag definitions, each flag ramped with three forced
groups and one group percentage. Dense model, 4 KiB pages, height rule:

| Body | Body bytes per definition | Page bytes per record | Pages |
| --- | --- | --- | --- |
| main (bool/int) | 47.5 | 62.1 | 3,245 |
| one-knob, fixed width | 58.9 | 73.2 | 3,077 |
| one-knob, varint | 35.3 | 49.4 | 2,874 |

The fixed-width body costs 18% more page bytes than main: every forced override grows from one
byte to eight, and a ramped flag carries an eight-byte rollout value. The varint body is 20%
smaller than main and 33% smaller than fixed width. The paged body is varint.

5M page bytes, the read bench and first read: pending the A3 jobs.
