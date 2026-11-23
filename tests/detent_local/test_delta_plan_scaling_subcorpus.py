"""The first N definitions-by-local-number of a DTCORP01 corpus, with every namespace record.

    python3 -I test_delta_plan_scaling_subcorpus.py IN OUT N

Keeps a definition whose local name is key<number> with number < N (the keys stay in order).
"""

import struct
import sys
from array import array


def main(src: str, dst: str, limit: int) -> None:
    with open(src, "rb") as handle:
        data = handle.read()
    assert data[:8] == b"DTCORP01"
    count, key_bytes, body_bytes = struct.unpack(">QQQ", data[8:32])
    at = 32
    key_offsets = array("I")
    key_offsets.frombytes(data[at : at + 4 * (count + 1)])
    key_offsets.byteswap()
    at += 4 * (count + 1)
    keys = data[at : at + key_bytes]
    at += key_bytes
    body_offsets = array("I")
    body_offsets.frombytes(data[at : at + 4 * (count + 1)])
    body_offsets.byteswap()
    at += 4 * (count + 1)
    bodies = data[at : at + body_bytes]
    kinds = data[at + body_bytes : at + body_bytes + count]
    assert len(kinds) == count
    out_keys, out_bodies, out_kinds = bytearray(), bytearray(), bytearray()
    ko, bo = array("I", [0]), array("I", [0])
    for i in range(count):
        key = keys[key_offsets[i] : key_offsets[i + 1]]
        if kinds[i] == 2:
            local = key.split(b"\0")[1]
            assert local.startswith(b"key")
            if int(local[3:]) >= limit:
                continue
        out_keys += key
        out_bodies += bodies[body_offsets[i] : body_offsets[i + 1]]
        out_kinds.append(kinds[i])
        ko.append(len(out_keys))
        bo.append(len(out_bodies))
    ko.byteswap()
    bo.byteswap()
    with open(dst, "wb") as out:
        out.write(
            b"DTCORP01"
            + struct.pack(">QQQ", len(out_kinds), len(out_keys), len(out_bodies))
        )
        out.writelines((ko.tobytes(), out_keys, bo.tobytes(), out_bodies, out_kinds))
    print(f"{dst}: {len(out_kinds)} records")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]))
