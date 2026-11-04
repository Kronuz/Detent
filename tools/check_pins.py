#!/usr/bin/env python3
"""Fails when cmake/dependencies.json pins a dependency differently from a library it pins.

For every manifest entry on github.com/Kronuz, fetches the library at the pinned commit and reads
the FetchContent_Declare calls in its CMakeLists.txt and *.cmake files. A declaration of a
repository the manifest also pins must carry the same commit (a tag is resolved with ls-remote).

    tools/check_pins.py [--manifest PATH] [--owner OWNER]
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import NamedTuple

SKIPPED_DIRS = re.compile(
    r"(^|/)(third_party|_deps|vendor|build[^/]*|bench|benchmarks|test|tests|examples|fuzz|docs)/"
)
SHA = re.compile(r"^[0-9a-f]{40}$")


class Declare(NamedTuple):
    library: str
    path: str
    name: str
    repository: str
    tag: str


def repo_key(url: str) -> str:
    """Normalizes a clone URL to `host/owner/name`."""
    url = url.strip().lower()
    url = re.sub(r"^(https?://|ssh://)?(git@)?", "", url)
    if "://" not in url:
        url = url.replace(":", "/", 1)
    return re.sub(r"(\.git)?/?$", "", url)


def parse_declares(library: str, path: str, text: str) -> list[Declare]:
    """Returns the FetchContent_Declare calls of one CMake file that name a repository and a tag."""
    text = re.sub(r"#[^\n]*", "", text)
    variables: dict[str, str] = {}
    for match in re.finditer(r'\bset\s*\(\s*(\w+)\s+"?([^\s")]+)"?', text, re.IGNORECASE):
        variables.setdefault(match[1], match[2])

    def expand(value: str) -> str:
        return re.sub(r"\$\{(\w+)\}", lambda m: variables.get(m[1], m[0]), value)

    declares: list[Declare] = []
    pattern = r"\bFetchContent_Declare\s*\(\s*([^\s()]+)([^)]*)\)"
    for match in re.finditer(pattern, text, re.IGNORECASE):
        url = re.search(r'\bGIT_REPOSITORY\s+"?([^\s")]+)', match[2])
        tag = re.search(r'\bGIT_TAG\s+"?([^\s")]+)', match[2])
        if url and tag:
            declares.append(Declare(library, path, match[1], expand(url[1]), expand(tag[1])))
    return declares


def run_git(*args: str, cwd: Path | None = None) -> str:
    result = subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True, timeout=300
    )
    return result.stdout


def library_declares(library: str, repository: str, revision: str, scratch: Path) -> list[Declare]:
    """Fetches `repository` at `revision` (as the build does) and parses its CMake files."""
    work = scratch / library
    work.mkdir()
    run_git("init", "-q", cwd=work)
    run_git("fetch", "-q", "--depth", "1", repository, revision, cwd=work)
    names = run_git("ls-tree", "-r", "--name-only", revision, cwd=work).splitlines()
    declares: list[Declare] = []
    for name in names:
        if not re.search(r"(^|/)CMakeLists\.txt$|\.cmake$", name) or SKIPPED_DIRS.search(name):
            continue
        text = run_git("show", f"{revision}:{name}", cwd=work)
        declares += parse_declares(library, name, text)
    return declares


def resolve(repository: str, tag: str, cache: dict[tuple[str, str], str]) -> str:
    """Returns the commit a GIT_TAG names: itself when a full SHA, else its peeled ls-remote ref."""
    if SHA.match(tag):
        return tag
    key = (repository, tag)
    if key not in cache:
        refs = run_git("ls-remote", repository, tag, f"{tag}^{{}}").splitlines()
        peeled = [line for line in refs if line.endswith("^{}")]
        chosen = (peeled or refs or [""])[0]
        cache[key] = chosen.split("\t")[0]
    return cache[key]


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--manifest", type=Path, default=Path("cmake/dependencies.json"))
    parser.add_argument("--owner", default="Kronuz")
    args = parser.parse_args()

    manifest: list[dict[str, str]] = json.loads(args.manifest.read_text())
    pinned = {repo_key(entry["repository"]): entry for entry in manifest}
    prefix = f"github.com/{args.owner.lower()}/"
    libraries = [e for e in manifest if repo_key(e["repository"]).startswith(prefix)]

    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(max_workers=8) as pool:
        futures = [
            pool.submit(library_declares, e["name"], e["repository"], e["revision"], Path(tmp))
            for e in libraries
        ]
        declares = [d for future in futures for d in future.result()]

    cache: dict[tuple[str, str], str] = {}
    checked = 0
    mismatches: list[str] = []
    for d in declares:
        entry = pinned.get(repo_key(d.repository))
        if entry is None:
            continue
        checked += 1
        commit = resolve(d.repository, d.tag, cache)
        if commit != entry["revision"]:
            shown = d.tag if commit == d.tag else f"{d.tag} = {commit or 'unresolved'}"
            mismatches.append(
                f"{d.library} ({d.path}) declares {entry['name']} at {shown};"
                f" the manifest pins {entry['revision']}"
            )
    for line in sorted(mismatches):
        print(f"::error::{line}")
    print(
        f"{len(libraries)} libraries, {checked} shared declarations, {len(mismatches)} mismatches"
    )
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
