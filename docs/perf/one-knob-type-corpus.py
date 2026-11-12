"""Converts a DTCORP01 corpus's definition bodies to one-knob bodies (docs/perf/one-knob-type.md).

  convert OLD NEW [--varint]   Rewrite every definition body from main's bool/int body to the
                               varint one-knob body (src/detent_local/paged_definition.cc) with
                               --varint, or to the fixed-width candidate without; print the census.

Keys, kinds and order are unchanged, so every corpus builds the same trie; only bodies differ.
The fixed-width candidate: revision u64, default i64, percentage u8 (255 none), flags u8, count
u16, rollout value i64 when flagged; each override is key length u8, kind u8, value i64, key."""

from __future__ import annotations

import json
import struct
import sys
from array import array
from pathlib import Path

MAGIC, KIND_DEFINITION = b"DTCORP01", 2


def leb(value: int) -> bytes:
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def zigzag(value: int) -> bytes:
    return leb((value << 1) ^ (value >> 63))


def parse_main(
    body: bytes,
) -> tuple[int, int, int | None, list[tuple[bytes, bool, int]]]:
    """main's body: (revision, default, percentage or None, [(key, is_percentage, value)])."""
    revision = int.from_bytes(body[0:8], "big")
    if body[8] == 2:
        assert len(body) == 24 and not body[9] and not body[10] and not body[11]
        return revision, int.from_bytes(body[16:24], "big", signed=True), None, []
    assert body[8] == 1 and body[9] <= 1 and not body[11]
    count = int.from_bytes(body[12:16], "big")
    rows, at = [], 24
    for _ in range(count):
        length = int.from_bytes(body[at : at + 2], "big")
        rows.append((body[at + 4 : at + 4 + length], bool(body[at + 2]), body[at + 3]))
        at += 4 + length
    assert at == len(body)
    return revision, body[9], None if body[10] == 255 else body[10], rows


def fixed(
    revision: int, default: int, pct: int | None, value: int | None, rows: list
) -> bytes:
    out = bytearray(struct.pack(">Qq", revision, default))
    out += bytes([255 if pct is None else pct, value is not None]) + struct.pack(
        ">H", len(rows)
    )
    if value is not None:
        out += struct.pack(">q", value)
    for key, percentage, v in rows:
        out += bytes([len(key), percentage]) + struct.pack(">q", v) + key
    return bytes(out)


def varint(
    revision: int, default: int, pct: int | None, value: int | None, rows: list
) -> bytes:
    out = bytearray(struct.pack(">Q", revision))
    out += (
        bytes([255 if pct is None else pct, value is not None])
        + zigzag(default)
        + leb(len(rows))
    )
    if value is not None:
        out += zigzag(value)
    for key, percentage, v in rows:
        out += (
            bytes([len(key), percentage])
            + (bytes([v]) if percentage else zigzag(v))
            + key
        )
    return bytes(out)


def convert(old: Path, new: Path, encode) -> dict[str, int]:
    data = old.read_bytes()
    if data[:8] != MAGIC:
        raise SystemExit(f"{old}: not a corpus file")
    count, key_bytes, body_bytes = struct.unpack(">QQQ", data[8:32])
    at = 32 + 4 * (count + 1)
    keys_part = data[32 : at + key_bytes]
    at += key_bytes
    offsets = array("I")
    offsets.frombytes(data[at : at + 4 * (count + 1)])
    offsets.byteswap()
    at += 4 * (count + 1)
    bodies = data[at : at + body_bytes]
    kinds = data[at + body_bytes : at + body_bytes + count]
    census = dict.fromkeys(
        (
            "definitions",
            "ints",
            "bools",
            "bools_ramped",
            "overrides",
            "forced",
            "percentages",
            "main_body_bytes",
            "new_body_bytes",
            "other_body_bytes",
        ),
        0,
    )
    out_bodies, out_offsets = bytearray(), array("I", [0])
    for i in range(count):
        body = bodies[offsets[i] : offsets[i + 1]]
        if kinds[i] == KIND_DEFINITION:
            revision, default, pct, rows = parse_main(body)
            boolean = body[8] == 1
            ramped = pct is not None or any(p for _, p, _ in rows)
            census["definitions"] += 1
            census["bools" if boolean else "ints"] += 1
            census["bools_ramped"] += boolean and ramped
            census["overrides"] += len(rows)
            census["percentages"] += sum(p for _, p, _ in rows)
            census["forced"] += sum(not p for _, p, _ in rows)
            census["main_body_bytes"] += len(body)
            body = encode(revision, default, pct, 1 if ramped else None, rows)
            census["new_body_bytes"] += len(body)
        else:
            census["other_body_bytes"] += len(body)
        out_bodies += body
        out_offsets.append(len(out_bodies))
    out_offsets.byteswap()
    with new.open("wb") as out:
        out.write(MAGIC + struct.pack(">QQQ", count, key_bytes, len(out_bodies)))
        out.write(keys_part)
        out.write(out_offsets.tobytes())
        out.write(out_bodies)
        out.write(kinds)
    return census


def main() -> None:
    args = sys.argv[1:]
    if (
        len(args) not in (3, 4)
        or args[0] != "convert"
        or args[3:] not in ([], ["--varint"])
    ):
        raise SystemExit(__doc__)
    census = convert(Path(args[1]), Path(args[2]), varint if args[3:] else fixed)
    print(json.dumps({"format": "varint" if args[3:] else "fixed", **census}))


if __name__ == "__main__":
    main()
