# Checks that dense_legacy_fixture.hh embeds exactly the files the frozen generator printed
# (the sha256 lines of its job log). Usage: dense_legacy_fixture_check.py HEADER LOG
from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path


def main(header: Path, log: Path) -> int:
    printed = re.finditer(
        r"^([0-9a-f]{64})  \S*/frozen-v2/(\S+)$", log.read_text(), re.MULTILINE
    )
    want = {m.group(2): m.group(1) for m in printed}
    got: dict[str, str] = {}
    for m in re.finditer(r'\{"([^"]+)",\s*((?:"[0-9a-f]*"\s*)+)\}', header.read_text()):
        data = bytes.fromhex("".join(re.findall(r'"([0-9a-f]*)"', m.group(2))))
        got[m.group(1)] = hashlib.sha256(data).hexdigest()
    for name in sorted(set(want) | set(got)):
        frozen = want.get(name)
        verdict = "ok" if got.get(name) == frozen else f"MISMATCH, frozen {frozen}"
        print(name, got.get(name), verdict)
    return 0 if want and got == want else 1


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]), Path(sys.argv[2])))
