from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

# `selected`, the agent store's table (src/detent_local/store_table.cc): a 64-byte header, entries of a
# length-prefixed name and fixed fields, then the SHA-256 of everything before it.
HEADER = 64
ENTRY_FIXED = 137
# The generation follows the name, the cursor's cluster, incarnation and position, and the slot.
GENERATION_AT = 16 + 16 + 8 + 4


def checked(table: bytes) -> bytes:
    if table[:8] != b"DTTABLE1" or len(table) < HEADER + 32:
        raise SystemExit("invalid store table")
    if hashlib.sha256(table[:-32]).digest() != table[-32:]:
        raise SystemExit("store table digest differs")
    return table


# A store's mark: its `change` file's inode, created once with the store, and its table's sequence.
def mark(directory: Path) -> tuple[int, int]:
    table = checked((directory / "selected").read_bytes())
    return (directory / "change").stat().st_ino, int.from_bytes(table[16:24], "big")


def selected_generation(table: bytes) -> str:
    checked(table)
    count = int.from_bytes(table[12:16], "big")
    if count != 1:
        raise SystemExit(f"store table holds {count} entries, expected 1")
    name_length = table[HEADER]
    if HEADER + ENTRY_FIXED + name_length != len(table) - 32:
        raise SystemExit("store table entry is truncated")
    at = HEADER + 1 + name_length + GENERATION_AT
    return table[at : at + 16].hex()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("format", choices=("flat-v1", "paged-v2", "mark", "same-store"))
    parser.add_argument("before", type=Path, nargs="?")
    args = parser.parse_args()
    directory: Path = args.directory
    expected: str = args.format
    if expected == "mark":
        print(*mark(directory))
        return
    if expected == "same-store":
        # The store continues: the same `change` file, and a table sequence that never goes back.
        before_path: Path = args.before
        inode, sequence = (int(field) for field in before_path.read_text().split())
        now_inode, now_sequence = mark(directory)
        if now_inode != inode or now_sequence < sequence:
            raise SystemExit(
                f"store replaced: change inode {inode} -> {now_inode}, sequence {sequence} -> {now_sequence}"
            )
        return
    image = (
        directory
        / f"generation-{selected_generation((directory / 'selected').read_bytes())}.image"
    )
    with image.open("rb") as stream:
        magic = stream.read(8)
    wanted = b"DTROOT03" if expected == "paged-v2" else b"DTIMAGE1"
    if magic != wanted:
        raise SystemExit(f"selected image format {magic!r}, expected {wanted!r}")


if __name__ == "__main__":
    main()
