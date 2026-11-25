from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

# `selected`, the agent store's table (DTTABLE1 layout 3, docs/LOCAL_IMAGE_FORMAT.md): a 72-byte header,
# an index of each page's SHA-256 and last name, the SHA-256 of the header and index, then 4,096-byte
# pages of entries: a length-prefixed name and 136 bytes of fixed fields. This is the tests' one reader
# of it; tests/detent_system/managed.py imports it.
HEADER = 72
PAGE = 4096
ENTRY_FIELDS = 136
# The fields start with the cursor's cluster and incarnation (32 bytes) and position (8); the
# generation follows them and the slot.
POSITION_AT = 32
GENERATION_AT = 16 + 16 + 8 + 4
# The selected generation's `.image` is its paged root (docs/LOCAL_IMAGE_FORMAT.md).
ROOT_MAGIC = b"DTROOT04"


def table_entries(table: bytes) -> dict[str, bytes] | None:
    """Each entry's fixed fields by name, or None unless the table is whole: its layout, its table
    digest, every page's digest and every entry inside its page."""
    if (
        len(table) < HEADER + 32
        or table[:8] != b"DTTABLE1"
        or int.from_bytes(table[8:12], "big") != 3
    ):
        return None
    digests: list[bytes] = []
    at = HEADER
    for _ in range(int.from_bytes(table[60:64], "big")):
        if at + 33 > len(table):
            return None
        digests.append(table[at : at + 32])
        at += 33 + table[at + 32]
    if hashlib.sha256(table[:at]).digest() != table[at : at + 32]:
        return None
    found: dict[str, bytes] = {}
    for k, digest in enumerate(digests):
        page = table[at + 32 + k * PAGE : at + 32 + (k + 1) * PAGE]
        if hashlib.sha256(page).digest() != digest:
            return None
        offset = 0
        while offset < len(page) and page[offset]:
            fields = offset + 1 + page[offset]
            if fields + ENTRY_FIELDS > len(page):
                return None
            found[page[offset + 1 : fields].decode()] = page[
                fields : fields + ENTRY_FIELDS
            ]
            offset = fields + ENTRY_FIELDS
    return found


def checked(table: bytes) -> dict[str, bytes]:
    found = table_entries(table)
    if found is None:
        raise SystemExit("invalid store table")
    return found


# A store's mark: its `change` file's inode, created once with the store, and its table's sequence.
def mark(directory: Path) -> tuple[int, int]:
    table = (directory / "selected").read_bytes()
    checked(table)
    return (directory / "change").stat().st_ino, int.from_bytes(table[16:24], "big")


def selected_generation(table: bytes) -> str:
    found = list(checked(table).values())
    if len(found) != 1:
        raise SystemExit(f"store table holds {len(found)} entries, expected 1")
    return found[0][GENERATION_AT : GENERATION_AT + 16].hex()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("format", choices=("root", "mark", "same-store"))
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
    if magic != ROOT_MAGIC:
        raise SystemExit(f"selected image format {magic!r}, expected {ROOT_MAGIC!r}")


if __name__ == "__main__":
    main()
