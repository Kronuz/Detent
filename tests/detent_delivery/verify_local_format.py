from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("format", choices=("flat-v1", "paged-v2"))
    args = parser.parse_args()
    directory: Path = args.directory
    expected: str = args.format
    selected = (directory / "selected").read_bytes()
    if selected[:8] != b"DTSELECT" or len(selected) < 32:
        raise SystemExit("invalid fixture selection")
    image = directory / f"generation-{selected[16:32].hex()}.image"
    with image.open("rb") as stream:
        magic = stream.read(8)
    wanted = b"DTROOT02" if expected == "paged-v2" else b"DTIMAGE1"
    if magic != wanted:
        raise SystemExit(f"selected image format {magic!r}, expected {wanted!r}")


if __name__ == "__main__":
    main()
