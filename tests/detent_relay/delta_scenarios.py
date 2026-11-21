"""Writes the dense-distribution model's delta scenarios for the C++ page window and D6 tests.

Usage: delta_scenarios.py CORPUS OUT [--streams] SPEC...

Per SPEC (u:K, c:K or m:U:I:D, docs/design/dense-distribution-model.py), OUT/SPEC.patch is a
DTCORP01 corpus of the records the scenario changes: each new or changed record at its new body,
and each removed key with kind 255 and no body. With --streams, OUT/SPEC.wire is the model's delta
transfer in wire form (its occurrence writes, in seal order), and one line per scenario gives its
pages, raw bytes and bytes as sent. Run with -I; the models are read from docs/design.
"""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path
from types import ModuleType

DESIGN = Path(__file__).resolve().parents[2] / "docs" / "design"
REMOVED = 255


def load(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def patch(D: ModuleType, base: object, after: object) -> object:
    writer = D.CorpusWriter()
    i = j = 0
    while i < base.count or j < after.count:
        a = base.key(i) if i < base.count else None
        b = after.key(j) if j < after.count else None
        if b is not None and (a is None or b < a):
            writer.add(b, after.kinds[j], after.body(j))
            j += 1
        elif a is not None and (b is None or a < b):
            writer.add(a, REMOVED, b"")
            i += 1
        else:
            if base.body(i) != after.body(j):
                writer.add(b, after.kinds[j], after.body(j))
            i, j = i + 1, j + 1
    return writer


def main(argv: list[str]) -> None:
    streams = "--streams" in argv
    args = [a for a in argv[1:] if a != "--streams"]
    if len(args) < 3:
        raise SystemExit(__doc__)
    corpus_path, out = Path(args[0]), Path(args[1])
    model = load("dense_distribution_model", DESIGN / "dense-distribution-model.py")
    D = model.load_dense(DESIGN / "dense-pages-model.py")
    model.D = D
    print(f"corpus {corpus_path} sha256 {hashlib.sha256(corpus_path.read_bytes()).hexdigest()}")
    corpus = D.read_corpus(corpus_path)
    out.mkdir(parents=True, exist_ok=True)
    base = model.encode(D, corpus)[0] if streams else None
    for spec in args[2:]:
        after = model.scenario(D, corpus, spec).corpus()
        patch(D, corpus, after).write(out / f"{spec}.patch")
        if not streams:
            print(f"{spec}: patch written", flush=True)
            continue
        target, encoder = model.encode(D, after)
        chosen = sorted(D.occurrence_diff(base, target).written)
        wire = model.make_transfer(D, target, encoder, chosen, "wire")
        (out / f"{spec}.wire").write_bytes(wire.stream)
        _, sent = model.fragments_sent(wire.stream, 3)
        root = D.state_commitment(target).hex()
        print(
            f"{spec}: pages {len(chosen)} raw {len(wire.stream)} sent {sent} root {root}",
            flush=True,
        )


if __name__ == "__main__":
    main(sys.argv)
