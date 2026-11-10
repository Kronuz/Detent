#!/usr/bin/env python3
"""Check every internal link and #anchor of a built Astro site.

usage: check_links.py DIST [BASE]      (BASE defaults to /Detent, the site's base path)
Exit status: 0 when every internal href resolves to a page or file and every fragment to an id
on its page (or is listed in link_exceptions.txt, which fails once the link is fixed), 1 otherwise.
"""

from __future__ import annotations

import os
import posixpath
import sys
from html.parser import HTMLParser
from typing import override
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []
        self.ids: set[str] = set()

    @override
    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        a = dict(attrs)
        if a.get("id"):
            self.ids.add(a["id"] or "")
        if tag == "a" and a.get("name"):
            self.ids.add(a["name"] or "")
        href = a.get("href")
        if tag == "a" and href is not None:
            self.links.append(href)


def load(dist: str) -> dict[str, Page]:
    pages: dict[str, Page] = {}
    for root, _, files in os.walk(dist):
        for name in files:
            if not name.endswith(".html"):
                continue
            rel = os.path.relpath(os.path.join(root, name), dist).replace(os.sep, "/")
            url = "/" + (rel[: -len("index.html")] if name == "index.html" else rel)
            page = Page()
            with open(os.path.join(root, name), encoding="utf-8") as f:
                page.feed(f.read())
            pages[url.rstrip("/") or "/"] = page
    return pages


def load_exceptions() -> dict[tuple[str, str], str]:
    """`PAGE HREF -- reason` lines of link_exceptions.txt: broken links an area quest still owes."""
    listed: dict[tuple[str, str], str] = {}
    path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "link_exceptions.txt"
    )
    if not os.path.exists(path):
        return listed
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f.read().splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            head, _, reason = line.partition(" -- ")
            parts = head.split()
            if len(parts) != 2 or not reason.strip():
                raise SystemExit(
                    f"link_exceptions.txt:{number}: expected 'PAGE HREF -- reason'"
                )
            listed[(parts[0], parts[1])] = reason.strip()
    return listed


def main(argv: list[str]) -> int:
    if not 2 <= len(argv) <= 3:
        print(__doc__)
        return 2
    dist = argv[1]
    base = (argv[2] if len(argv) == 3 else "/Detent").rstrip("/")
    pages = load(dist)
    if not pages:
        print(f"no pages under {dist}")
        return 1
    broken: dict[str, list[str]] = {}
    total = 0
    for src, page in sorted(pages.items()):
        # Pages sit at the dist root; the base path only prefixes their URLs.
        here = base + src if src != "/" else base + "/"
        for href in page.links:
            u = urlsplit(href)
            if u.scheme or u.netloc:
                continue
            total += 1
            path = unquote(u.path)
            target = (
                here
                if not path
                else path
                if path.startswith("/")
                else posixpath.normpath(posixpath.join(here.rstrip("/") + "/", path))
            )
            if not (target == base or target.startswith(base + "/")):
                if not os.path.exists(os.path.join(dist, target.lstrip("/"))):
                    broken.setdefault(src, []).append(href)
                continue
            key = (target[len(base) :] or "/").rstrip("/") or "/"
            if key in pages:
                if (
                    u.fragment
                    and u.fragment not in pages[key].ids
                    and not u.fragment.startswith(":~:")
                ):
                    broken.setdefault(src, []).append(href)
            elif not os.path.exists(os.path.join(dist, key.lstrip("/"))):
                broken.setdefault(src, []).append(href)
    listed = load_exceptions()
    found = {(src, href) for src, hrefs in broken.items() for href in hrefs}
    new = sorted(found - listed.keys())
    stale = sorted(listed.keys() - found)
    for src, href in new:
        print(f"BROKEN {src}: {href}")
    for src, href in stale:
        print(f"STALE exception {src} {href}")
    print(
        f"link check: {len(pages)} pages, {total} internal links, {len(found)} broken, {len(new)} unlisted, {len(stale)} stale"
    )
    return 1 if new or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
