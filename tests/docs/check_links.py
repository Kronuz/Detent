#!/usr/bin/env python3
"""Check every internal link and #anchor of a built Astro site.

usage: check_links.py DIST [BASE]      (BASE defaults to /Detent, the site's base path)
Exit status: 0 when every internal href resolves to a page or file and every fragment to an id
on its page, 1 when any is broken.
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
    for src, hrefs in broken.items():
        print(f"BROKEN {src}: {', '.join(sorted(set(hrefs)))}")
    print(
        f"link check: {len(pages)} pages, {total} internal links, {sum(len(set(h)) for h in broken.values())} broken"
    )
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
