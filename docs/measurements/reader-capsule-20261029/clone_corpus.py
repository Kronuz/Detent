from __future__ import annotations

import ctypes
import fcntl
import hashlib
import json
import os
import stat
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOURCE = REPO / ".scratch/fast-client-build/managed-5m-r3/generations"


def identity(path: Path) -> tuple[int, int, int, int, int]:
    value = path.lstat()
    if not stat.S_ISREG(value.st_mode) or value.st_nlink != 1:
        raise ValueError(f"Expected a private regular file: {path}")
    return (
        value.st_dev,
        value.st_ino,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def main() -> None:
    destinations = (HERE / "legacy/generations", HERE / "capsul/generations")
    if any(path.exists() for path in destinations):
        raise ValueError("Refuse to replace existing trial corpora")
    libc = ctypes.CDLL(None, use_errno=True)
    clonefile = libc.clonefile
    clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32]
    clonefile.restype = ctypes.c_int
    selected = (SOURCE / "selected").read_bytes()
    if len(selected) != 176 or selected[:8] != b"DTSELECT":
        raise ValueError("Unexpected selected framing")
    lease_name = "generation-" + selected[16:32].hex() + ".lease"
    rows: list[dict[str, object]] = []
    with (SOURCE / lease_name).open("rb") as lease:
        fcntl.flock(lease.fileno(), fcntl.LOCK_SH | fcntl.LOCK_NB)
        if (SOURCE / "selected").read_bytes() != selected:
            raise ValueError("Selection changed during lease acquisition")
        paths = sorted(SOURCE.iterdir())
        for destination in destinations:
            destination.mkdir(parents=True, mode=0o700)
        for path in paths:
            before = identity(path)
            copies: list[dict[str, object]] = []
            for destination in destinations:
                copied = destination / path.name
                if clonefile(os.fsencode(path), os.fsencode(copied), 0):
                    code = ctypes.get_errno()
                    raise OSError(code, os.strerror(code), str(copied))
                after = identity(copied)
                if before[:2] == after[:2] or before[2] != after[2]:
                    raise ValueError("Clone aliases source or changes size")
                source_stat, copy_stat = path.stat(), copied.stat()
                if (source_stat.st_uid, stat.S_IMODE(source_stat.st_mode)) != (
                    copy_stat.st_uid,
                    stat.S_IMODE(copy_stat.st_mode),
                ):
                    raise ValueError("Clone changes owner or mode")
                copies.append({"path": str(copied), "identity": after})
            if identity(path) != before:
                raise ValueError("Source changed during cloning")
            rows.append({"name": path.name, "source": before, "copies": copies})
        if (SOURCE / "selected").read_bytes() != selected:
            raise ValueError("Selection changed during cloning")
    (HERE / "corpus-clones.json").write_text(
        json.dumps(
            {
                "method": "APFS clonefile, independent inodes; selected generation leased while cloning. No live source mutation.",
                "selected_sha256": hashlib.sha256(selected).hexdigest(),
                "files": rows,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
