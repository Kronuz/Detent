#!/usr/bin/env python3
"""Show that the docs checks notice drift: each mutant must make its check fail.

    mutants.py --build-dir DIR [--deps-from DIR]

M1 changes one asserted value on a page; M2 renames one CLI flag in the source and rebuilds;
M3 renames one configuration key on a reference page. M1 and M2 must fail run_e2e.py, M3
check_docs.py. Exit status: 0 when every mutant is killed, 1 when one survives.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DOCS = Path("website/src/content/docs")


def mutant_tree(name: str, copy_source: bool) -> Path:
    """A root with the real tests and sidebar, a private copy of the pages, and optionally of src."""
    tree = ROOT / ".scratch" / "mutants" / name
    shutil.rmtree(tree, ignore_errors=True)
    (tree / "website" / "src").mkdir(parents=True)
    (tree / "tests").symlink_to(ROOT / "tests")
    (tree / "website" / "src" / "lib").symlink_to(ROOT / "website" / "src" / "lib")
    shutil.copytree(ROOT / DOCS, tree / DOCS)
    if copy_source:
        for entry in ("src", "include", "cmake", "CMakeLists.txt"):
            source = ROOT / entry
            if source.is_dir():
                shutil.copytree(source, tree / entry)
            else:
                shutil.copy2(source, tree / entry)
    return tree


def edit(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    assert old in text, f"{path}: {old!r} not found"
    path.write_text(text.replace(old, new))


def run(cmd: list[str], env: dict[str, str] | None = None) -> int:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, env=env, check=False).returncode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", type=Path, required=True)
    ap.add_argument("--deps-from", type=Path)
    args = ap.parse_args()
    e2e = [sys.executable, "-I", str(HERE / "run_e2e.py")]
    survived: list[str] = []

    tree = mutant_tree("m1", copy_source=False)
    edit(
        tree / DOCS / "settings-and-flags.md",
        "stdout-matches: ^500$",
        "stdout-matches: ^501$",
    )
    rc = run(
        [
            *e2e,
            "--root",
            str(tree),
            "--build-dir",
            str(args.build_dir),
            "--page",
            "quickstart",
            "--page",
            "settings-and-flags",
        ]
    )
    print(
        f"M1 asserted value 500 -> 501: {'killed' if rc == 1 else 'SURVIVED'} (exit {rc})"
    )
    if rc != 1:
        survived.append("M1")

    tree = mutant_tree("m3", copy_source=False)
    edit(
        tree / DOCS / "reference" / "configuration.md",
        "defaults_path",
        "defaults_pathx",
    )
    env = {**os.environ, "DETENT_DOCS_ROOT": str(tree)}
    rc = run([sys.executable, "-I", str(HERE / "check_docs.py")], env)
    print(
        f"M3 config key defaults_path renamed on a page: {'killed' if rc == 1 else 'SURVIVED'} (exit {rc})"
    )
    if rc != 1:
        survived.append("M3")

    tree = mutant_tree("m2", copy_source=True)
    for path in (tree / "src" / "detent_agent").glob("*.cc"):
        text = path.read_text()
        if '"--generation-dir"' in text:
            path.write_text(text.replace('"--generation-dir"', '"--generation-dirx"'))
    cmd = [*e2e, "--root", str(tree), "--fresh-build", "--page", "quickstart"]
    if args.deps_from:
        cmd += ["--deps-from", str(args.deps_from)]
    rc = run(cmd)
    print(
        f"M2 CLI flag --generation-dir renamed in the source: {'killed' if rc == 1 else 'SURVIVED'} (exit {rc})"
    )
    if rc != 1:
        survived.append("M2")
    for name in ("m1", "m2", "m3"):
        shutil.rmtree(ROOT / ".scratch" / "mutants" / name, ignore_errors=True)
    print(
        "mutants:", "all killed" if not survived else "survived " + ", ".join(survived)
    )
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
