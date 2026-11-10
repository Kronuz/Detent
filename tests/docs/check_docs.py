#!/usr/bin/env python3
"""Two-way check between the reference pages and the source.

Kinds: CLI flags, configuration keys, HTTP endpoints, error/status names and knob types.
`docs-only` names are on a page but not in the source; `source-only` names are in the source but
on no page. A gap is allowed only when listed in docs_exceptions.txt, one per line:

    docs-only|source-only KIND NAME -- reason

A listed gap that no longer exists is an error too, so the list only shrinks.
Exit status: 0 clean, 1 on any unlisted gap or stale exception, 2 on a malformed exceptions file.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(os.environ.get("DETENT_DOCS_ROOT") or Path(__file__).resolve().parents[2])
DOCS = ROOT / "website" / "src" / "content" / "docs"
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
EXCEPTIONS = Path(__file__).with_name("docs_exceptions.txt")

Names = dict[str, set[str]]


def read_all(directory: Path, suffixes: tuple[str, ...]) -> dict[Path, str]:
    return {
        p: p.read_text(errors="replace")
        for p in sorted(directory.rglob("*"))
        if p.suffix in suffixes
    }


def page_text(*relative: str) -> str:
    return "\n".join((DOCS / r).read_text() for r in relative)


def all_docs() -> str:
    return "\n".join(t for t in read_all(DOCS, (".md", ".mdx")).values())


def all_source() -> dict[Path, str]:
    return read_all(SRC, (".cc", ".hh", ".h", ".cpp")) | read_all(
        INCLUDE, (".hh", ".h")
    )


def table_firsts(text: str) -> set[str]:
    """Backticked words in the first column of every table row."""
    found: set[str] = set()
    for line in text.splitlines():
        if line.startswith("|"):
            cell = line.split("|")[1]
            found.update(re.findall(r"`([A-Za-z_][\w]*)`", cell))
    return found


def json_keys(text: str) -> set[str]:
    blocks = re.findall(r"^```json\n(.*?)^```", text, re.DOTALL | re.MULTILINE)
    return {k for b in blocks for k in re.findall(r'"([a-z][a-z0-9_]*)"\s*:', b)}


# ---------------------------------------------------------------------------- flags

CLI_PAGES = (
    "reference/cli/detent.md",
    "reference/cli/detent-agent.md",
    "reference/cli/detentd.md",
)
FLAG = re.compile(r"--[a-z][a-z0-9]*(?:-[a-z0-9]+)*")


def flags() -> tuple[set[str], set[str]]:
    docs = set(FLAG.findall(page_text(*CLI_PAGES)))
    source: set[str] = set()
    for path, text in all_source().items():
        if path.parts[len(SRC.parts)] in ("detent_agent", "detent_cli", "detentd"):
            source.update(
                f
                for f in re.findall(r'"(--[a-z][a-z0-9-]*)"', text)
                if not f.endswith("-")
            )
    # The reader limits are one `--reader-<field>` family built from the field list.
    fields = re.search(
        r"reader_fields\[\]\s*\{(.*?)\}",
        (SRC / "detent_agent" / "reader_capacity.hh").read_text(),
        re.DOTALL,
    )
    if fields:
        source.update(
            "--reader-" + f.replace("_", "-")
            for f in re.findall(r'"(\w+)"', fields.group(1))
        )
    return docs, source


# ---------------------------------------------------------------------------- config keys

CONFIG_PAGES = (
    "reference/configuration.md",
    "guides/managed-delivery.md",
    "operations/authority.md",
    "reference/cli/detentd.md",
)


def config_keys() -> tuple[set[str], set[str]]:
    text = page_text(*CONFIG_PAGES)
    docs = json_keys(text) | {
        k
        for k in table_firsts(text)
        if k.islower() and ("_" in k or k in ("port", "token", "ca", "keys"))
    }
    source: set[str] = set()
    for text in all_source().values():
        for body in re.findall(
            r"\bfields\(\s*\w+\s*,\s*\{(.*?)\}\s*\)", text, re.DOTALL
        ):
            source.update(re.findall(r'"([a-z][a-z0-9_]*)"', body))
    for rel in ("detentd/main.cc", "detent_agent/config.cc"):
        source.update(re.findall(r'\{"([a-z_]+)",\s*\{"--', (SRC / rel).read_text()))
    return docs, source


# ---------------------------------------------------------------------------- endpoints

ENDPOINT = re.compile(r"/v[0-9]/[a-z][a-z_/-]*[a-z]")


def endpoints() -> tuple[set[str], set[str]]:
    docs = set(
        ENDPOINT.findall(
            page_text(
                "reference/http-api.md",
                "reference/http-api/health.md",
                "reference/http-api/authority.md",
            )
        )
    )
    source: set[str] = set()
    for text in all_source().values():
        source.update(re.findall(r'"(/v[0-9]/[a-z][a-z_/-]*)"', text))
    return docs, source


# ---------------------------------------------------------------------------- errors and statuses


def enum_members(text: str, name: str) -> set[str]:
    match = re.search(rf"enum class {name}\s*\{{(.*?)\}}", text, re.DOTALL)
    assert match, name
    return set(
        re.findall(
            r"^\s*([A-Z]\w*)", re.sub(r"//[^\n]*", "", match.group(1)), re.MULTILINE
        )
    )


def errors() -> tuple[set[str], set[str]]:
    sources = all_source()
    errs = (INCLUDE / "detent" / "errors.hh").read_text()
    source = enum_members(errs, "ErrorCategory") | enum_members(
        errs, "UpdateRejectReason"
    )
    for text in sources.values():
        source.update(re.findall(r'\\"error\\":\\"(\w+)\\"', text))
    service = (SRC / "detent_authority" / "service.cc").read_text()
    names = re.search(
        r"constexpr const char \*names\[\] = \{(.*?)\};", service, re.DOTALL
    )
    assert names
    source.update(re.findall(r'"(\w+)"', names.group(1)))
    errors_page = page_text("reference/errors.md")
    authority = (DOCS / "reference/http-api/authority.md").read_text()
    outcomes = authority.split("### Outcomes", 1)[1].split("\n---", 1)[0]
    docs = table_firsts(errors_page) | table_firsts(outcomes)
    docs |= set(re.findall(r'"error":\s*"(\w+)"', all_docs()))
    return docs, source


def errors_documented(name: str) -> bool:
    return re.search(rf"\b{re.escape(name)}\b", all_docs()) is not None


# ---------------------------------------------------------------------------- knob types


def knob_types() -> tuple[set[str], set[str]]:
    source = set(
        re.findall(r'type == "(\w+)"', (SRC / "detent" / "json_schema.cc").read_text())
    )
    docs = set(re.findall(r'"type"\s*:\s*"(\w+)"', all_docs())) | table_firsts(
        page_text("reference/knobs.md")
        .split("## Knob Types", 1)[1]
        .split("\n---", 1)[0]
    )
    return docs, source


# ---------------------------------------------------------------------------- driver


def gaps() -> set[tuple[str, str, str]]:
    found: set[tuple[str, str, str]] = set()
    spaces = {
        "flag": flags(),
        "config": config_keys(),
        "endpoint": endpoints(),
        "error": errors(),
        "type": knob_types(),
    }
    text = all_docs()
    for kind, (docs, source) in spaces.items():
        for name in docs - source:
            found.add(("docs-only", kind, name))
        for name in source - docs:
            # Any page may document a source name; the page sets above only decide what to verify.
            if kind in ("flag", "config", "endpoint", "type") and re.search(
                rf"(?<![\w-]){re.escape(name)}(?![\w-])", text
            ):
                continue
            if kind == "error" and errors_documented(name):
                continue
            found.add(("source-only", kind, name))
    return found


def load_exceptions() -> dict[tuple[str, str, str], str]:
    listed: dict[tuple[str, str, str], str] = {}
    if not EXCEPTIONS.exists():
        return listed
    for number, line in enumerate(EXCEPTIONS.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        head, _, reason = line.partition(" -- ")
        parts = head.split()
        if (
            len(parts) != 3
            or parts[0] not in ("docs-only", "source-only")
            or not reason.strip()
        ):
            print(
                f"{EXCEPTIONS.name}:{number}: expected 'docs-only|source-only KIND NAME -- reason'"
            )
            sys.exit(2)
        listed[(parts[0], parts[1], parts[2])] = reason.strip()
    return listed


def main() -> int:
    found = gaps()
    listed = load_exceptions()
    new = sorted(found - listed.keys())
    stale = sorted(listed.keys() - found)
    for direction, kind, name in new:
        print(f"GAP {direction} {kind} {name}")
    for direction, kind, name in stale:
        print(f"STALE exception {direction} {kind} {name}")
    print(
        f"docs check: {len(found)} gaps, {len(listed)} listed, {len(new)} unlisted, {len(stale)} stale"
    )
    return 1 if new or stale else 0


if __name__ == "__main__":
    sys.exit(main())
