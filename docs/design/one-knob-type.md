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

## Evaluation

`get(name, hash, group) -> int64`: a CLI override, else a forced group value, else a group
percentage, else the knob-wide percentage, else `default`; each percentage goes through
`detail::ramp()`, which returns `rollout_value` when a hash is present and
`stable_bucket(name, hash) < pct`, and `default` otherwise. The bucket is unchanged, so a caller in
a flag's bucket at main is in it here. `get_as<T>` converts with a range check; `get_as<bool>`
accepts 0 and 1 and nothing else (`ConversionFailed`). `eval` is gone. The full order is in
[knob-model.md](knob-model.md#evaluation).

## JSON

```json
{ "default": 25, "overrides": { "beta": 7, "canary": { "rollout_pct": 50 } },
  "rollout_pct": 10, "rollout_value": 50 }
```

A flag is `{"default": 0, "rollout_pct": 10, "rollout_value": 1}`. There is no `type`; a `type`
field, a boolean literal or a fraction is refused, as are unknown fields.

## Why no strings (D65)

Strings stay out. Every value is eight bytes in the flat image and one varint in a page, so a
record has a fixed shape and a read returns a register, never a borrowed span whose lifetime ties
the caller to a page or an image. A string value would bring back a second type, a length-prefixed
body, a lifetime contract on the read API, and a size budget per value at publish. The owner's
ruling (D65) keeps the single integer type until a use case pays for that.

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

The 100K corpus has 100,000 definitions (52,022 integers, 47,978 flags, each flag ramped with
three forced groups and one group percentage) and 4 namespace records; the 5M corpus has 2.5M of
each kind with the same shape. Page bytes come from the dense model (`dense-pages-model.py pack
CORPUS 4096 --rule=height`), job 261111-154349-807bef; for main's corpus the model's 3,245 pages
and 6,209,298 bytes equal main's C++ full build exactly (`test_local_dense_fullbuild`, in the
pages jobs below, which also build the varint tree).

The columns:

1. Body: the definition body only, as the page stores it after the node's 4-byte header and the
   key suffix, averaged over definitions (the converter's census).
2. Page bytes: the sum of every page's length over records (definitions plus namespace records).
   A page's length is its 64-byte header, its nodes (a leaf is 4 bytes, the key suffix and the
   body; a branch is its label prefix and child slots) and 68 bytes per external child. Pages
   are never padded: 4 KiB is the most a page may hold, not its size.
3. Pages, and the mean page length (page bytes over pages).

| Corpus | Body | Body B/def | Page B/record | Pages | Mean page B |
| --- | --- | --- | --- | --- | --- |
| 100K | main (bool/int) | 47.5 | 62.1 | 3,245 | 1,913 |
| 100K | one-knob, fixed width | 58.9 | 73.2 | 3,077 | 2,380 |
| 100K | one-knob, varint | 35.3 | 49.4 | 2,874 | 1,720 |
| 5M | main (bool/int) | 48.5 | 63.1 | 162,367 | 1,943 |
| 5M | one-knob, fixed width | 60.5 | 74.9 | 154,334 | 2,426 |
| 5M | one-knob, varint | 36.5 | 50.6 | 144,544 | 1,751 |

Page count does not follow page bytes: the height rule seals a subtree as its own page where a
branch's children overflow 4 KiB, so the count is set by where subtrees cross that budget. The
fixed-width body has fewer, fuller pages than main (mean 2,380 B against 1,913 B) and still 18%
more bytes; the bytes are what a reader maps, hashes and pages in.

The fixed-width body costs 18% (100K) and 19% (5M) more page bytes than main: every forced
override grows from one byte to eight, and a ramped flag carries an eight-byte rollout value. The
varint body is 20% smaller than main at both sizes and 32% to 33% smaller than fixed width. The paged body is varint.

The read bench and first read: pending the A3 jobs.
