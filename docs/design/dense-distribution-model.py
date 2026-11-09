"""Dense distribution model: what a receiver is sent, and does, when DTPAGE03 state arrives as pages.

Status: DRAFT (2026-11-08). The numbers behind docs/design/dense-distribution.md. It imports the
dense model (docs/design/dense-pages-model.py, pinned at Detent:28d3ed9) for the corpus, the packer
and the encoder, and adds the wire forms, the modeled records and a receiver that rebuilds the
target's files from a transfer and checks the one root digest. Byte counts are exact for the
corpus; record bytes are modeled and calibrated against the measured 5M record copy; no time here
is measured (elapsed seconds are the model's own, informational).

Modes:
  full CORPUS               The full copy: pages, packs and three wire forms, raw and with zstd per
                            1 MiB segment and per 64 KiB fragment; the records it replaces; the
                            receiver's SHA-256 input and writes against a full build, today's and
                            dense; a receiver replay that must reproduce the packs byte for byte.
  delta CORPUS SCENARIO...  Per scenario, the pages the target has and the base lacks (occurrence
                            writes, dense-pages D7) in the same wire forms, against the records
                            answer; a receiver replay over the base. Scenarios: u:K (K uniform
                            updates), c:K (K consecutive definitions), m:U:I:D (the dense-pages
                            T4 churn and seed).
  day CORPUS                Pages only against records per agent-day: four per-poll deltas at
                            1- and 10-minute polls, with dollars for 10,000 agents.
  stale CORPUS GAP...       Per gap HOURS:SHAPE (moderate, hotrange or uniform churn at 1M writes
                            a day), a stale receiver's top-down pull of the pages it lacks, level
                            by level, with the stop rule, against the head's full copy and the
                            records answer: bytes as sent, round trips, receiver SHA-256 input.
                            Record only: the pull is not part of the design (D10, PLAN.md D36).
  check CORPUS              Every mode on a small corpus, plus the receiver's refusals: damage,
                            reuse and truncation; a layout or an order the sender chose; and a
                            validly sealed forgery per structure and canonicity check, refused by
                            that check and accepted by a mutant without it.

Options: --dense=PATH (the dense model; default $JOB_REF_DENSE/docs/design/dense-pages-model.py).
Needs Python 3.14 (compression.zstd). Run under a light hold, as a campaign job, e.g.:
  campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light --minutes 60 \\
      -- /usr/local/bin/python3 -I docs/design/dense-distribution-model.py full CORPUS
"""

from __future__ import annotations

import hashlib
import importlib.util
import itertools
import os
import random
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Any

FRAGMENT = (
    64 * 1024
)  # today's fragment payload bound (include/detent_distribution/framing.hh:7)
FRAGMENT_HEADER = 96  # framing.hh:7
SNAPSHOT_HEADER = 160  # snapshot frame header, cursors and counts; a modeled constant

# Measured record full copies (docs/perf/cold-bootstrap-5m.md, "Wire bytes of the record full
# copy"): records -> (raw decoded bytes, bytes as sent: zstd-3 fragments plus 96-byte headers).
MEASURED_RECORDS = {5_000_004: (261_337_255, 25_646_713), 100_004: (4_894_345, 542_302)}

D: Any = None  # the dense model, loaded by load_dense()


def load_dense(path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location("dense_pages_model", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules["dense_pages_model"] = module
    spec.loader.exec_module(module)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    print(f"dense model {path} sha256 {digest}")
    return module


def zstd_size(data: bytes, level: int) -> int:
    from compression import zstd

    return len(zstd.compress(data, level=level)) if data else 0


def fragments_sent(stream: bytes, level: int = 3) -> tuple[int, int]:
    """Fragments of at most 64 KiB, each compressed alone, plus a 96-byte header each."""
    count = (len(stream) + FRAGMENT - 1) // FRAGMENT
    sent = sum(
        zstd_size(stream[at : at + FRAGMENT], level)
        for at in range(0, len(stream), FRAGMENT)
    )
    return count, sent + FRAGMENT_HEADER * count


# --- wire forms ---------------------------------------------------------------------------------
# A transfer is the pages a receiver lacks, back to back in the target's seal order, with no
# padding and no segment table: the receiver lays them out by the build's rule (transfer_place:
# 8-byte offsets, a new 1 MiB segment when a page does not fit, segments numbered from 1). Page
# bytes are the stored bytes with the fields the receiver derives rewritten:
#   "stored"  the bytes as the sender holds them (placement in the sender's segment ids);
#   "digests" placement of an in-transfer child is (segment number, offset) in that derived
#             layout, of a base child zero; every digest kept, so a bad page is found where it is;
#   "wire"    as "digests", with the header digest and every in-transfer child's digest zeroed:
#             the receiver computes them bottom-up and checks only the root.
#   "pull"    a stale receiver's page answer (D10): every content digest kept, since the receiver
#             compares them with its base to choose the next level, header digest and every
#             placement entry zero.

FORMS = ("stored", "digests", "wire")
CAPACITY = 4096
# The receiver's checks, each of which a mutant (receive(..., skip={name})) turns off.
CHECKS = ("layout", "order", "parse", "labels", "keys", "height", "canonical")


@dataclass(slots=True)
class Transfer:
    stream: bytes  # what the wire carries
    segments: list[
        bytes
    ]  # the layout the receiver derives from the stream, padding included
    parts: list[bytes]  # the stream cut at the derived segment breaks
    pages: int
    page_bytes: int


Place = list[tuple[int, int]]


def transfer_place(D: Any, lengths: list[int]) -> Place:
    """(segment number from 1, offset) of each page, laid out as the encoder lays out a build."""
    out: Place = []
    number, used = 0, D.SEGMENT
    for length in lengths:
        start = (used + 7) & ~7
        if start + length > D.SEGMENT:
            number, start = number + 1, 0
        out.append((number, start))
        used = start + length
    return out


def lay_out(pages: list[bytes] | list[bytearray], places: Place) -> list[bytes]:
    segments: list[bytearray] = []
    for page, (number, offset) in zip(pages, places, strict=True):
        while len(segments) < number:
            segments.append(bytearray())
        segment = segments[number - 1]
        segment.extend(bytes(offset - len(segment)))
        segment.extend(page)
    return [bytes(s) for s in segments]


def make_transfer(
    D: Any,
    packing: Any,
    encoder: Any,
    chosen: list[int],
    form: str,
    place: Any = None,
) -> Transfer:
    """The transfer of pages `chosen`, in that order. `place` replaces transfer_place only to
    forge a sender that names its own segment breaks."""
    memory = encoder.memory
    at_place = {
        (D.segment_id(o), off): page for page, (o, off) in encoder.placed.items()
    }
    lengths = [packing.pages[p].length for p in chosen]
    places = (place or transfer_place)(D, lengths)
    where = dict(zip(chosen, places, strict=True))
    pages: list[bytes] = []
    for p in chosen:
        o, off = encoder.placed[p]
        length = packing.pages[p].length
        page = bytearray(memory[D.segment_id(o)][off : off + length])
        if form == "pull":
            externals = D.be(page, 12, 2)
            page[length - 20 * externals : length] = bytes(20 * externals)
            page[32:64] = bytes(32)
        elif form != "stored":
            externals = D.be(page, 12, 2)
            content, placement = length - 68 * externals, length - 20 * externals
            for j in range(externals):
                at = placement + 20 * j
                child = at_place[(bytes(page[at : at + 16]), D.be(page, at + 16, 4))]
                if child in where:
                    tn, toff = where[child]
                    page[at : at + 20] = (
                        bytes(12) + tn.to_bytes(4, "big") + toff.to_bytes(4, "big")
                    )
                    if form == "wire":
                        page[content + 48 * j : content + 48 * j + 32] = bytes(32)
                else:
                    page[at : at + 20] = bytes(20)
            if form == "wire":
                page[32:64] = bytes(32)
        pages.append(bytes(page))
    parts: list[bytes] = []
    for page, (number, _) in zip(pages, places, strict=True):
        if len(parts) < number:
            parts.append(b"")
        parts[number - 1] += page
    return Transfer(
        b"".join(pages), lay_out(pages, places), parts, len(chosen), sum(lengths)
    )


def report_transfer(name: str, transfer: Transfer) -> dict[str, int]:
    row = {
        "raw": len(transfer.stream),
        "seg_z1": sum(zstd_size(s, 1) for s in transfer.parts),
        "seg_z3": sum(zstd_size(s, 3) for s in transfer.parts),
        "seg_z9": sum(zstd_size(s, 9) for s in transfer.parts),
    }
    row["frags"], row["frag_z3"] = fragments_sent(transfer.stream, 3)
    print(
        f"  {name:>7}: {transfer.pages:,} pages, stream {row['raw']:,} B, laid out in"
        f" {len(transfer.segments):,} segments; zstd per segment -1 {row['seg_z1']:,} -3"
        f" {row['seg_z3']:,} -9 {row['seg_z9']:,}; zstd-3 per 64 KiB fragment, headers included,"
        f" {row['frag_z3']:,} ({row['frags']:,} fragments)",
        flush=True,
    )
    return row


# --- the receiver ------------------------------------------------------------------------------


class Refused(Exception):
    pass


def external_paths(page: bytes | bytearray, top: bytes) -> list[bytes]:
    """The trie path below each external slot, in slot order: the bytes before the child's top
    node prefix. Generic bytes; nothing assumes text or NUL-free keys."""
    externals = int.from_bytes(page[12:14], "big")
    paths: list[bytes] = [b""] * externals

    def node(at: int, path: bytes) -> None:
        tag = page[at]
        if tag != 1:
            return
        plen, count = page[at + 1], page[at + 2] + 1
        prefix = bytes(page[at + 3 : at + 3 + plen])
        labels = page[at + 3 + plen : at + 3 + plen + count]
        slots = at + 3 + plen + count
        for i in range(count):
            value = int.from_bytes(page[slots + 2 * i : slots + 2 * i + 2], "big")
            below = path + prefix + bytes((labels[i],))
            if value & 0x8000:
                paths[value & 0x7FFF] = below
            else:
                node(value, below)

    node(64, top)
    return paths


@dataclass(slots=True)
class Base:
    """What a receiver's base generation answers: the page at a trie path, with its summary and
    its placement. The model indexes by path; the agent walks its base along the path."""

    by_path: dict[bytes, tuple[bytes, int, int, int, bytes, int, int]]
    # path -> digest, keys, length, height, segment id, offset, pages walked from the root


def base_index(D: Any, packing: Any, encoder: Any) -> Base:
    depth = [0] * len(packing.pages)
    for p in range(len(packing.pages) - 1, -1, -1):
        parent = packing.pages[p].parent
        depth[p] = 1 if parent < 0 else depth[parent] + 1
    by_path = {}
    for p, page in enumerate(packing.pages):
        o, off = encoder.placed[p]
        by_path[page.path] = (
            page.digest,
            page.keys,
            page.length,
            page.height,
            D.segment_id(o),
            off,
            depth[p],
        )
    return Base(by_path)


@dataclass(slots=True)
class Received:
    segments: list[bytes]
    root: bytes
    hashed: int
    digests: int
    base_children: int
    base_walk_pages: int
    order: list[int]  # the seal order the receiver computed, as transfer page indices


@dataclass(slots=True)
class Node:
    leaf: bool
    size: int  # node bytes
    prefix: bytes  # a branch's prefix
    labels: bytes
    slots: list[int]  # per child: its node index in the page, or ~j for content entry j


@dataclass(slots=True)
class Parsed:
    nodes: list[Node]  # preorder, the top node first
    entries: list[tuple[int, int, int]]  # content entries: keys, length, height


def parse_page(D: Any, page: bytes | bytearray, skip: frozenset[str]) -> Parsed:
    """dense-pages D4.3 in one preorder parse; refuses what a first touch refuses."""
    length, externals, count = len(page), D.be(page, 12, 2), D.be(page, 14, 2)
    content = length - 68 * externals
    nodes: list[Node] = []
    entries: list[tuple[int, int, int]] = []
    leaves = next_external = 0

    def structure(ok: bool, what: str) -> None:
        if not ok and "parse" not in skip:
            raise Refused(f"page structure: {what}")

    def node(at: int) -> int:
        nonlocal leaves, next_external
        tag = page[at]
        if tag in (2, 3):
            size = 4 + page[at + 1] + D.be(page, at + 2, 2)
            nodes.append(Node(True, size, b"", b"", []))
            leaves += 1
            structure(at + size <= content, "a node past the content table")
            return at + size
        if tag != 1:
            raise Refused("page structure: node tag")
        plen, children = page[at + 1], page[at + 2] + 1
        labels = bytes(page[at + 3 + plen : at + 3 + plen + children])
        slots = at + 3 + plen + children
        end = slots + 2 * children
        structure(end <= content, "a node past the content table")
        record = Node(
            False,
            3 + plen + 3 * children,
            bytes(page[at + 3 : at + 3 + plen]),
            labels,
            [],
        )
        nodes.append(record)
        if "labels" not in skip and (
            children < 2 or any(a >= b for a, b in itertools.pairwise(labels))
        ):
            raise Refused("branch labels: fewer than two, or not strictly ascending")
        for k in range(children):
            value = D.be(page, slots + 2 * k, 2)
            if value & 0x8000:
                structure(
                    value & 0x7FFF == next_external, "external slots out of order"
                )
                record.slots.append(~(value & 0x7FFF))
                next_external += 1
            else:
                structure(value == end, "an in-page slot off its preorder offset")
                record.slots.append(len(nodes))
                end = node(value)
        return end

    try:
        structure(64 <= content and not any(page[25:32]), "header")
        end = node(64)
        structure(
            end == content and len(nodes) == count and next_external == externals,
            "nodes do not fill [64, L - 68E) in N nodes and E external slots",
        )
        for j in range(externals):
            at = content + 48 * j
            keys, clen, height = (
                D.be(page, at + 32, 8),
                D.be(page, at + 40, 4),
                page[at + 44],
            )
            structure(
                keys >= 1
                and 79 <= clen <= D.MAX_PAGE
                and height >= 1
                and not any(page[at + 45 : at + 48]),
                "content entry",
            )
            entries.append((keys, clen, height))
    except IndexError:
        raise Refused("page structure: truncated") from None
    if "keys" not in skip and D.be(page, 16, 8) != leaves + sum(e[0] for e in entries):
        raise Refused("header keys disagree with the page")
    if "height" not in skip and page[24] != 1 + max((e[2] for e in entries), default=0):
        raise Refused("header height disagrees with the page")
    return Parsed(nodes, entries)


def split_pages(D: Any, stream: bytes) -> list[bytearray]:
    pages: list[bytearray] = []
    at = 0
    while at < len(stream):
        if stream[at : at + 8] != b"DTPAGE03":
            raise Refused("bytes outside a page")
        length = D.be(stream, at + 8, 4)
        if not 79 <= length <= D.MAX_PAGE or at + length > len(stream):
            raise Refused("page length")
        pages.append(bytearray(stream[at : at + length]))
        at += length
    return pages


def receive_stream(
    D: Any,
    stream: bytes,
    base: Base | None,
    expected_root: bytes | None,
    own_id: Any,
    skip: frozenset[str] = frozenset(),
) -> Received:
    """Receives a transfer as the wire carries it: pages back to back, laid out by the build's
    rule. expected_root None skips the root check, only to compute a forger's root."""
    pages = split_pages(D, stream)
    return receive_pages(
        D,
        pages,
        transfer_place(D, [len(p) for p in pages]),
        base,
        expected_root,
        own_id,
        skip,
    )


def receive(
    D: Any,
    segments: list[bytes],
    base: Base | None,
    expected_root: bytes | None,
    own_id: Any,
    skip: frozenset[str] = frozenset(),
) -> Received:
    """Receives a transfer handed over already laid out in segments, as a sender that names its
    own breaks would. The layout is derived from the page lengths all the same: placement that
    names the sender's layout and not the derived one is refused ("layout" keeps the sender's)."""
    pages: list[bytearray] = []
    given: Place = []
    for n, segment in enumerate(segments, start=1):
        at = 0
        while at < len(segment):
            if segment[at : at + 8] != b"DTPAGE03":
                raise Refused("bytes outside a page")
            length = D.be(segment, at + 8, 4)
            if not 79 <= length <= D.MAX_PAGE or at + length > len(segment):
                raise Refused("page length")
            pages.append(bytearray(segment[at : at + length]))
            given.append((n, at))
            end = at + length
            at = min(len(segment), (end + 7) & ~7)
            if any(segment[end:at]):
                raise Refused("nonzero padding")
    places = given if "layout" in skip else transfer_place(D, [len(p) for p in pages])
    return receive_pages(D, pages, places, base, expected_root, own_id, skip)


def receive_pages(
    D: Any,
    pages: list[bytearray],
    places: Place,
    base: Base | None,
    expected_root: bytes | None,
    own_id: Any,
    skip: frozenset[str],
) -> Received:
    """Rebuilds the target's segments: every page parsed (dense-pages D4.3), the tree walked from
    the root (the last page) with D5 rerun at every node (D9 canonicity) and the seal order it
    implies compared with the transfer's, base children resolved by path, every digest computed
    bottom-up, and the root compared. Refuses on any inconsistency."""
    if not pages:
        if base is None and expected_root == D.EMPTY_COMMITMENT:
            return Received(
                [], expected_root, 0, 0, 0, 0, []
            )  # publishes an empty state
        if base is not None and base.by_path.get(b"", (b"",))[0] == expected_root:
            return Received([], expected_root, 0, 0, 0, 0, [])  # the base is the target
        raise Refused("empty transfer")
    parsed = [parse_page(D, page, skip) for page in pages]
    index = {place: i for i, place in enumerate(places)}
    budget = CAPACITY - D.HEADER
    resolved: dict[
        tuple[int, int], int | tuple[bytes, int, int, int, bytes, int, int]
    ] = {}
    used: set[int] = set()
    order: list[int] = []
    walk = 0

    def resolve(i: int, j: int, path: bytes) -> int:
        nonlocal walk
        page = pages[i]
        at = len(page) - 20 * len(parsed[i].entries) + 20 * j
        tn, toff = D.be(page, at + 12, 4), D.be(page, at + 16, 4)
        if any(page[at : at + 12]):
            raise Refused("placement names no transfer segment")
        if tn or base is None:
            child = index.get((tn, toff))
            if child is None:
                raise Refused("placement names no page of the derived layout")
            if child >= i:
                raise Refused("child not before its parent")
            if child in used:
                raise Refused("page referenced twice")
            used.add(child)
            resolved[(i, j)] = child
            return child
        found = base.by_path.get(path)
        if found is None:
            raise Refused("base lacks a page the delta reuses")
        resolved[(i, j)] = found
        walk += found[6]
        return -1

    def visit(i: int, k: int, path: bytes) -> tuple[int, int]:
        """The cluster (bytes, height) of node k of page i, as D5 builds it; emits seal events."""
        node, top = parsed[i].nodes[k], k == 0
        alone = top and len(pages[i]) > CAPACITY  # a page sealed alone, at its own node
        if node.leaf:
            if (
                "canonical" not in skip
                and node.size > budget
                and not (alone and len(parsed[i].nodes) == 1)
            ):
                raise Refused("page not packed by D5: a leaf over budget is not alone")
            if alone and i != len(pages) - 1:
                order.append(i)
            return node.size, 0
        # Children as D5 sees them: (bytes, height, transfer page or -1, sealed here, alone).
        items: list[tuple[int, int, int, bool, bool]] = []
        total = node.size
        for c, slot in enumerate(node.slots):
            below = path + node.prefix + bytes((node.labels[c],))
            if slot >= 0:
                size, height = visit(i, slot, below)
                items.append((size, height, -1, False, False))
                total += size
                continue
            j = ~slot
            _, clen, cheight = parsed[i].entries[j]
            child = resolve(i, j, below)
            if child >= 0:
                visit(child, 0, below)
            self_sealed = clen > CAPACITY
            items.append(
                (clen - D.HEADER, cheight, child, not self_sealed, self_sealed)
            )
            total += D.EXTERNAL if self_sealed else clen - D.HEADER
        candidates = [
            c for c, item in enumerate(items) if not item[4] and item[0] > D.EXTERNAL
        ]

        def rank(c: int) -> tuple[int, int, int]:
            # A sealed child's cluster is its page less the header, one page less high.
            size, height, _, here, _ = items[c]
            return (height - 1 if here else height, -size, c)

        sealed: list[int] = []
        for c in sorted(candidates, key=rank):
            if total <= budget:
                break
            sealed.append(c)
            total -= items[c][0] - D.EXTERNAL
        here = [c for c, item in enumerate(items) if item[3]]
        if "canonical" not in skip:
            if sorted(sealed) != here:
                raise Refused("page not packed by D5: its sealed children differ")
            if total > budget and not alone:
                raise Refused("page not packed by D5: a node over budget is not a page")
        for c in sorted(here, key=rank):
            if items[c][2] >= 0:
                order.append(items[c][2])
        if alone and i != len(pages) - 1:
            order.append(i)
        return total, max(item[1] for item in items)

    root = len(pages) - 1
    limit = sys.getrecursionlimit()
    sys.setrecursionlimit(max(limit, 10_000))
    try:
        visit(root, 0, b"")
    finally:
        sys.setrecursionlimit(limit)
    order.append(root)
    if len(used) != root:
        raise Refused("a page no parent references")
    if "order" not in skip and order != list(range(len(pages))):
        raise Refused("transfer is not in seal order")
    # Bottom-up in transfer order: children are earlier pages (checked by resolve).
    done: list[tuple[bytes, int, int, int]] = []
    hashed = 0
    for i, page in enumerate(pages):
        length, externals = len(page), len(parsed[i].entries)
        content, placement = length - 68 * externals, length - 20 * externals
        for j in range(externals):
            entry, at = content + 48 * j, placement + 20 * j
            target = resolved[(i, j)]
            if isinstance(target, int):
                summary = done[target]
                tn, toff = places[target]
                page[at : at + 20] = own_id(tn) + toff.to_bytes(4, "big")
            else:
                digest, bkeys, blen, bheight, sid, boff, _ = target
                summary = (digest, bkeys, blen, bheight)
                page[at : at + 20] = sid + boff.to_bytes(4, "big")
            stated = bytes(page[entry : entry + 32])
            if stated != bytes(32) and stated != summary[0]:
                raise Refused("content digest mismatch")
            if parsed[i].entries[j] != summary[1:]:
                raise Refused("content summary mismatch")
            page[entry : entry + 32] = summary[0]
        h = hashlib.sha256(page[0:32])
        h.update(page[64:placement])
        hashed += placement - 32
        digest = h.digest()
        stated = bytes(page[32:64])
        if stated != bytes(32) and stated != digest:
            raise Refused("page digest mismatch")
        page[32:64] = digest
        done.append((digest, D.be(page, 16, 8), length, page[24]))
    if expected_root is not None and done[root][0] != expected_root:
        raise Refused("root digest mismatch")
    bases = sum(1 for t in resolved.values() if not isinstance(t, int))
    return Received(
        lay_out(pages, places), done[root][0], hashed, len(pages), bases, walk, order
    )


# --- records, as today's wire carries them ------------------------------------------------------
# A row is MessagePack [namespace index, local name, revision, definition] (snapshot_encoder.cc:
# record_row); a tombstone carries nil for the definition, and a namespace row [name, revision].
# The definition is modeled as the stored body after its 8-byte revision, as a bin: the wire's
# typed encoding differs, so full-copy totals calibrate the model against the measured copy.


def mp_uint(v: int) -> bytes:
    if v < 128:
        return bytes((v,))
    for tag, width in ((0xCC, 1), (0xCD, 2), (0xCE, 4), (0xCF, 8)):
        if v < 1 << (8 * width):
            return bytes((tag,)) + v.to_bytes(width, "big")
    raise ValueError(v)


def mp_str(b: bytes) -> bytes:
    if len(b) < 32:
        return bytes((0xA0 | len(b),)) + b
    if len(b) < 256:
        return bytes((0xD9, len(b))) + b
    return bytes((0xDA,)) + len(b).to_bytes(2, "big") + b


def mp_bin(b: bytes) -> bytes:
    if len(b) < 256:
        return bytes((0xC4, len(b))) + b
    return bytes((0xC5,)) + len(b).to_bytes(2, "big") + b


def split_key(key: bytes) -> tuple[bytes, bytes]:
    """Today's store grammar, for the record model only: K ns NUL local NUL, or W ns NUL."""
    end = key.index(0, 1)
    return key[1:end], key[end + 1 : -1]


class RecordModel:
    def __init__(self, D: Any, corpus: Any) -> None:
        self.D = D
        names = sorted(
            split_key(corpus.key(i))[0]
            for i in range(corpus.count)
            if corpus.kinds[i] == D.KIND_NAMESPACE
        )
        self.ns = {name: n for n, name in enumerate(names)}

    def row(self, key: bytes, kind: int, body: bytes | None) -> bytes:
        name, local = split_key(key)
        if kind == self.D.KIND_NAMESPACE:
            assert body is not None
            return b"\x92" + mp_str(name) + mp_uint(self.D.be(body, 0, 8))
        head = b"\x94" + mp_uint(self.ns.get(name, len(self.ns))) + mp_str(local)
        if body is None:
            return head + b"\x00" + b"\xc0"
        return head + mp_uint(self.D.be(body, 0, 8)) + mp_bin(body[8:])

    def stream(self, rows: list[bytes]) -> bytes:
        return bytes(SNAPSHOT_HEADER) + b"".join(rows)


def records_row(
    name: str, stream: bytes, scale: tuple[float, float]
) -> dict[str, float]:
    frags, sent = fragments_sent(stream, 3)
    row = {"raw": len(stream), "sent": sent, "frags": frags}
    row["raw_cal"], row["sent_cal"] = len(stream) * scale[0], sent * scale[1]
    print(
        f"  records {name}: modeled raw {len(stream):,} B, as sent {sent:,} B ({frags:,} fragments);"
        f" calibrated raw {row['raw_cal']:,.0f} B, as sent {row['sent_cal']:,.0f} B",
        flush=True,
    )
    return row


def calibrate(D: Any, corpus: Any) -> tuple[float, float]:
    """The modeled record full copy against the measured one for this corpus's size (1 if none)."""
    model = RecordModel(D, corpus)
    stream = model.stream(
        [
            model.row(corpus.key(i), corpus.kinds[i], corpus.body(i))
            for i in range(corpus.count)
        ]
    )
    frags, sent = fragments_sent(stream, 3)
    raw, measured = MEASURED_RECORDS.get(corpus.count, (len(stream), sent))
    scale = (raw / len(stream), measured / sent)
    print(
        f"  records full copy: modeled raw {len(stream):,} B, as sent {sent:,} B ({frags:,} fragments);"
        f" measured raw {raw:,} B, as sent {measured:,} B;"
        f" calibration raw x{scale[0]:.4f}, sent x{scale[1]:.4f}",
        flush=True,
    )
    return scale


# --- full copy ----------------------------------------------------------------------------------


def encode(D: Any, corpus: Any) -> tuple[Any, Any]:
    encoder = D.Encoder(memory=True, compress=False)
    packing = D.pack_dense(corpus, 4096, "height", encoder)
    return packing, encoder


def full_main(D: Any, corpus: Any, replay: bool = True) -> tuple[float, float]:
    started = time.monotonic()
    packing, encoder = encode(D, corpus)
    pages = packing.pages
    total = sum(p.length for p in pages)
    root = D.state_commitment(packing)
    print(
        f"dense build: {len(pages):,} pages, {total:,} page B, {encoder.segments:,} segments of"
        f" {encoder.raw:,} B; page digest input {encoder.hashed:,} B in {len(pages):,} digests;"
        f" root {root.hex()}; {time.monotonic() - started:.0f} s",
        flush=True,
    )
    print(
        f"dense build SHA-256: pages {encoder.hashed:,} + packs {encoder.raw:,} ="
        f" {encoder.hashed + encoder.raw:,} B in {len(pages) + encoder.segments:,} digests;"
        f" writes {encoder.raw:,} B"
    )
    today = D.build_today(corpus)
    t_total = sum(today.size)
    t_segments, t_bytes = D.segments_for(today.size)
    t_logical = sum(today.verify) - t_total
    t_hashed = t_total + t_logical + t_bytes
    print(
        f"today's build SHA-256: physical {t_total:,} + logical {t_logical:,} + packs {t_bytes:,} ="
        f" {t_hashed:,} B in {2 * len(today.size) + t_segments:,} digests; writes {t_bytes:,} B in"
        f" {t_segments:,} segments",
        flush=True,
    )
    del today
    chosen = list(range(len(pages)))
    print("full copy transfer:")
    rows = {
        f: report_transfer(f, make_transfer(D, packing, encoder, chosen, f))
        for f in FORMS
    }
    scale = calibrate(D, corpus)
    for f in FORMS:
        print(
            f"  {f}: per 64 KiB fragment / records as sent (measured) ="
            f" {rows[f]['frag_z3'] / (sent := MEASURED_RECORDS.get(corpus.count, (0, 1))[1]):.3f};"
            f" per segment zstd-3 / records = {rows[f]['seg_z3'] / sent:.3f}"
        )
    if replay:
        wire = make_transfer(D, packing, encoder, chosen, "wire")
        t0 = time.monotonic()
        got = receive_stream(D, wire.stream, None, root, D.segment_id_from_one)
        same = got.segments == [
            encoder.memory[D.segment_id(o)] for o in range(encoder.segments)
        ]
        print(
            f"receiver replay (wire form): root {'equal' if got.root == root else 'DIFFERENT'};"
            f" files {'identical to the build' if same else 'DIFFERENT'}; SHA-256 page input"
            f" {got.hashed:,} B in {got.digests:,} digests, plus packs {encoder.raw:,} B in"
            f" {encoder.segments:,} for .live; writes {encoder.raw:,} B; {time.monotonic() - t0:.0f} s"
        )
        assert got.root == root and same and got.hashed == encoder.hashed
    print(f"full: {time.monotonic() - started:.0f} s", flush=True)
    return scale


# --- deltas -------------------------------------------------------------------------------------


def clustered(D: Any, corpus: Any, rng: random.Random, size: int) -> Any:
    definitions = [
        i for i in range(corpus.count) if corpus.kinds[i] == D.KIND_DEFINITION
    ]
    first = rng.randrange(len(definitions) - size)
    updated = set(definitions[first : first + size])
    touched = {corpus.watermark[i] for i in updated}
    bump = 1 << 32
    writer = D.CorpusWriter()
    for i in range(corpus.count):
        body = corpus.body(i)
        if i in updated or i in touched:
            body = (D.be(body, 0, 8) + bump).to_bytes(8, "big") + body[8:]
        writer.add(corpus.key(i), corpus.kinds[i], body)
    return writer


def scenario(D: Any, corpus: Any, spec: str) -> Any:
    kind, *numbers = spec.split(":")
    n = [int(x) for x in numbers]
    if kind == "u":
        return D.mutate(corpus, random.Random(0x554E49), n[0], 0, 0)[0]
    if kind == "c":
        return clustered(D, corpus, random.Random(0x434C5553), n[0])
    if kind == "m":  # dense-pages T4: the churn seed and counts
        return D.mutate(corpus, random.Random(0x434855524E), n[0], n[1], n[2])[0]
    raise SystemExit(f"unknown scenario {spec}")


def record_answer(
    model: RecordModel, base: Any, after: Any
) -> tuple[bytes, int, int, int]:
    """Every key whose record differs, at its new value; a tombstone per removed key."""
    rows: list[bytes] = []
    i = j = changed = removed = 0
    while i < base.count or j < after.count:
        a = base.key(i) if i < base.count else None
        b = after.key(j) if j < after.count else None
        if b is not None and (a is None or b < a):
            rows.append(model.row(b, after.kinds[j], after.body(j)))
            changed, j = changed + 1, j + 1
        elif a is not None and (b is None or a < b):
            rows.append(model.row(a, base.kinds[i], None))
            removed, i = removed + 1, i + 1
        else:
            if base.body(i) != after.body(j):
                rows.append(model.row(b, after.kinds[j], after.body(j)))
                changed += 1
            i, j = i + 1, j + 1
    return model.stream(rows), changed, removed, len(rows)


def delta_main(
    D: Any, corpus: Any, specs: list[str], scale: tuple[float, float]
) -> dict[str, tuple[int, float]]:
    """Per scenario: pages in the wire form as sent, and records as sent (calibrated)."""
    out: dict[str, tuple[int, float]] = {}
    started = time.monotonic()
    base, base_encoder = encode(D, corpus)
    index = base_index(D, base, base_encoder)
    model = RecordModel(D, corpus)
    print(
        f"base: {len(base.pages):,} pages; {time.monotonic() - started:.0f} s",
        flush=True,
    )
    for spec in specs:
        t0 = time.monotonic()
        after = scenario(D, corpus, spec).corpus()
        target, encoder = encode(D, after)
        diff = D.occurrence_diff(base, target)
        chosen = sorted(diff.written)
        novel = {p.digest for p in base.pages}
        digest_novel = sum(1 for p in chosen if target.pages[p].digest not in novel)
        root = D.state_commitment(target)
        print(
            f"delta {spec}: {after.count:,} records; occurrence writes {len(chosen):,} pages"
            f" ({digest_novel:,} with content the base lacks), reused {len(diff.reused):,};"
            f" root {root.hex()}",
            flush=True,
        )
        rows = {
            f: report_transfer(f, make_transfer(D, target, encoder, chosen, f))
            for f in FORMS
        }
        stream, changed, removed, _ = record_answer(model, corpus, after)
        print(
            f"  records answer: {changed:,} rows at new values, {removed:,} tombstones"
        )
        rec = records_row(spec, stream, scale)
        wire = make_transfer(D, target, encoder, chosen, "wire")
        got = receive_stream(
            D, wire.stream, index, root, lambda n: D.segment_id(1_000_000 + n)
        )
        assert got.root == root
        print(
            f"  receiver replay: root equal; SHA-256 page input {got.hashed:,} B in {got.digests:,}"
            f" digests; base children resolved {got.base_children:,} (walks of at most"
            f" {got.base_walk_pages:,} pages in all)"
        )
        for f in ("digests", "wire"):
            print(
                f"  {spec} {f}: pages / records, as sent (calibrated) ="
                f" {rows[f]['frag_z3'] / rec['sent_cal']:.2f}; raw = {rows[f]['raw'] / rec['raw_cal']:.2f};"
                f" pages {rows[f]['frag_z3']:,} B, records {rec['sent_cal']:,.0f} B"
            )
        out[spec] = (rows["wire"]["frag_z3"], rec["sent_cal"])
        print(f"  {time.monotonic() - t0:.0f} s", flush=True)
    return out


# --- a day of polls -----------------------------------------------------------------------------

AGENTS, DOLLARS_PER_GB = 10_000, 0.02
CADENCES = ((1, 1440), (10, 144))  # poll every N minutes -> polls a day
DAY_SPECS = (
    "u:1",  # one update per poll
    "m:9800:100:100",  # dense-pages T4 churn per poll
    "u:694",  # namespace D6 moderate churn (1M writes a day at 5M) per 1-minute poll
    "u:6944",  # the same churn per 10-minute poll
)


def day_main(D: Any, corpus: Any, scale: tuple[float, float]) -> None:
    rows = delta_main(D, corpus, list(DAY_SPECS), scale)
    print(
        f"per agent-day, pages only (wire form) against records, as sent; dollars for {AGENTS:,}"
        f" agents at ${DOLLARS_PER_GB}/GB:"
    )
    for minutes, polls in CADENCES:
        for spec in DAY_SPECS:
            pages, records = rows[spec]
            day_pages, day_records = pages * polls, records * polls
            print(
                f"  every {minutes} min ({polls:,} polls), {spec} per poll: pages {day_pages:,} B"
                f" (${day_pages * AGENTS / 1e9 * DOLLARS_PER_GB:,.2f}), records {day_records:,.0f} B"
                f" (${day_records * AGENTS / 1e9 * DOLLARS_PER_GB:,.2f}), ratio"
                f" {day_pages / day_records:.1f}x",
                flush=True,
            )


# --- a stale receiver ---------------------------------------------------------------------------
# A base older than the relay's windows: the receiver pulls the pages it lacks top-down from the
# relay's head, one batch per level, comparing each child's content entry with its base page at
# the same trie path; the stop rule estimates the pull from a sample of its subtrees and takes
# the full copy when the rest of the pull would cost more (D10).

WRITES_PER_DAY = (
    1_000_000  # namespace D6's moderate churn at 5M (namespace-delivery.md:181)
)
TURNOVER, HOT_SHARE = (
    0.01,
    0.8,
)  # its creates and deletes, and its writes to a hot 1% of keys
REQUEST_HEADER = (
    96  # a pull request's frame, modeled as one fragment header; 32 B per digest
)
SAMPLE_LEVEL = (
    256  # the stop rule samples the first level of the pull with this many pages
)
SAMPLE_SHARE = (
    16  # and pulls the subtrees of a sixteenth of them first, chosen by digest
)
SHAPES = (
    "moderate",
    "hotrange",
    "uniform",
)  # hot 1% scattered / contiguous / no hot set


def gap(D: Any, corpus: Any, hours: float, shape: str) -> tuple[Any, int]:
    """The records after `hours` of churn at WRITES_PER_DAY (scaled to the corpus): T4's mix of
    98% updates, 1% inserts beside existing keys and 1% deletes; updates go 80% to a hot 1% of
    definitions unless the shape is uniform. Each key keeps its last write. Returns the writer
    and the number of writes."""
    if shape not in SHAPES:
        raise SystemExit(f"unknown gap shape {shape}")
    rng = random.Random(0x5354414C45)
    definitions = [
        i for i in range(corpus.count) if corpus.kinds[i] == D.KIND_DEFINITION
    ]
    n = len(definitions)
    writes = round(WRITES_PER_DAY * hours / 24 * corpus.count / 5_000_004)
    creates = deletes = round(writes * TURNOVER)
    updates = writes - creates - deletes
    hot_writes = 0 if shape == "uniform" else round(writes * HOT_SHARE)
    if shape == "moderate":
        hot = rng.sample(range(n), n // 100)
    else:
        first = rng.randrange(n - n // 100)
        hot = list(range(first, first + n // 100))
    picked = {hot[rng.randrange(len(hot))] for _ in range(hot_writes)}
    picked.update(rng.randrange(n) for _ in range(updates - hot_writes))
    deleted = {definitions[d] for d in rng.sample(range(n), deletes)}
    beside = [definitions[d] for d in rng.sample(range(n), creates)]
    updated = {definitions[d] for d in picked} - deleted
    bump = 1 << 32
    touched: set[int] = set()
    records: dict[bytes, tuple[int, bytes]] = {}
    for i in range(corpus.count):
        key, body = corpus.key(i), corpus.body(i)
        if i in deleted:
            touched.add(corpus.watermark[i])
            continue
        if i in updated:
            body = (D.be(body, 0, 8) + bump).to_bytes(8, "big") + body[8:]
            touched.add(corpus.watermark[i])
        records[key] = (corpus.kinds[i], body)
    for i in beside:
        records[corpus.key(i)[:-1] + b"x\0"] = (D.KIND_DEFINITION, corpus.body(i))
        touched.add(corpus.watermark[i])
    for w in touched:
        key = corpus.key(w)
        kind, body = records[key]
        records[key] = (kind, (D.be(body, 0, 8) + bump).to_bytes(8, "big"))
    writer = D.CorpusWriter()
    for key in sorted(records):
        kind, body = records[key]
        writer.add(key, kind, body)
    return writer, writes


def pull_levels(target: Any, index: Base) -> dict[int, list[int]]:
    """The receiver's top-down pull: level 1 is the root; a child joins the next level unless the
    base page at its trie path has its digest, keys, length and height. Target pages by level."""
    children: dict[int, list[int]] = {}
    for p, page in enumerate(target.pages):
        children.setdefault(page.parent, []).append(p)
    levels: dict[int, list[int]] = {}
    top = target.pages[target.root]
    held = index.by_path.get(b"")
    if held is not None and held[:4] == (top.digest, top.keys, top.length, top.height):
        return levels
    frontier, depth = [target.root], 1
    while frontier:
        levels[depth] = sorted(frontier)
        nxt: list[int] = []
        for p in frontier:
            for c in children.get(p, []):
                page = target.pages[c]
                held = index.by_path.get(page.path)
                if held is None or held[:4] != (
                    page.digest,
                    page.keys,
                    page.length,
                    page.height,
                ):
                    nxt.append(c)
        frontier, depth = nxt, depth + 1
    return levels


def level_cost(D: Any, target: Any, encoder: Any, pages: list[int]) -> tuple[int, int]:
    """One pull round trip for `pages`: the answer as sent (pull form) plus its request, and the
    pages' raw bytes."""
    if not pages:
        return 0, 0
    answer = make_transfer(D, target, encoder, sorted(pages), "pull")
    _, sent = fragments_sent(answer.stream, 3)
    return sent + REQUEST_HEADER + 32 * len(pages), answer.page_bytes


def stop_rule(
    D: Any,
    target: Any,
    encoder: Any,
    levels: dict[int, list[int]],
    costs: dict[int, int],
    full_sent: int,
) -> tuple[str, int, int]:
    """D10's stop rule. At the first level with SAMPLE_LEVEL pages the receiver first pulls, to the
    bottom, the subtrees of the pages whose digest starts below 256 / SAMPLE_SHARE, and scales
    their cost to the level's other pages by the keys their content entries state. What it spent
    is spent either way, so it takes the full copy when that estimate of the rest passes the full
    copy's size, and otherwise pulls the rest. A pull with no level that wide runs to the end.
    Returns (what it did, bytes, round trips)."""
    depths = sorted(levels)
    first = next((d for d in depths if len(levels[d]) >= SAMPLE_LEVEL), None)
    if first is None:
        return "no level to sample; pull", sum(costs.values()), len(depths)
    before = sum(costs[d] for d in depths if d < first)
    top = {p: p for p in levels[first]}
    for d in depths:
        if d > first:
            for p in levels[d]:
                top[p] = top[target.pages[p].parent]
    sample = {
        p for p in levels[first] if target.pages[p].digest[0] < 256 // SAMPLE_SHARE
    }
    if not sample:
        sample = {min(levels[first], key=lambda p: target.pages[p].digest)}
    below = [d for d in depths if d >= first]
    sampled = [[p for p in levels[d] if top[p] in sample] for d in below]
    rest = [[p for p in levels[d] if top[p] not in sample] for d in below]
    sample_cost = sum(level_cost(D, target, encoder, pages)[0] for pages in sampled)
    keys = sum(target.pages[p].keys for p in levels[first])
    sample_keys = sum(target.pages[p].keys for p in sample)
    estimate = round(sample_cost * (keys - sample_keys) / sample_keys)
    spent = before + sample_cost
    rounds = first - 1 + sum(1 for pages in sampled if pages)
    rest_cost = sum(level_cost(D, target, encoder, pages)[0] for pages in rest)
    rest_rounds = sum(1 for pages in rest if pages)
    print(
        f"  sample: level {first}, {len(sample):,} of {len(levels[first]):,} pages and their"
        f" subtrees, {sample_cost:,} B; decided after {spent:,} B in {rounds} round trips; the"
        f" rest estimated {estimate:,} B (it costs {rest_cost:,} B) against the full copy"
        f" {full_sent:,} B"
    )
    if estimate > full_sent:
        return (
            f"full copy after the sample at level {first}",
            spent + full_sent,
            rounds + 1,
        )
    return (
        f"pull after the sample at level {first}",
        spent + rest_cost,
        rounds + rest_rounds,
    )


def stale_main(
    D: Any, corpus: Any, specs: list[str], scale: tuple[float, float]
) -> None:
    """Per gap (HOURS:SHAPE): the pull per level, the stop rule, the full copy of the head and
    the records answer, with bytes as sent, round trips and the receiver's SHA-256 input."""
    started = time.monotonic()
    base, base_encoder = encode(D, corpus)
    index = base_index(D, base, base_encoder)
    model = RecordModel(D, corpus)
    print(
        f"base: {len(base.pages):,} pages; {time.monotonic() - started:.0f} s",
        flush=True,
    )
    for spec in specs:
        t0 = time.monotonic()
        hours_text, shape = spec.split(":")
        writer, writes = gap(D, corpus, float(hours_text), shape)
        after = writer.corpus()
        target, encoder = encode(D, after)
        diff = D.occurrence_diff(base, target)
        levels = pull_levels(target, index)
        pulled = sorted(p for level in levels.values() for p in level)
        assert pulled == sorted(diff.written), "the pull fetches the occurrence writes"
        root = D.state_commitment(target)
        everything = list(range(len(target.pages)))
        full = make_transfer(D, target, encoder, everything, "wire")
        _, full_sent = fragments_sent(full.stream, 3)
        stream, changed, removed, _ = record_answer(model, corpus, after)
        _, records_sent = fragments_sent(stream, 3)
        records_cal = records_sent * scale[1]
        delta = make_transfer(D, target, encoder, pulled, "wire")
        _, delta_sent = fragments_sent(delta.stream, 3)
        print(
            f"stale {spec}: {writes:,} writes; {after.count:,} records, {changed:,} rows at new"
            f" values, {removed:,} tombstones; root {root.hex()}",
            flush=True,
        )
        sent = raw = requests = stop_sent = 0
        stop: int | None = None
        rounds = 0
        costs: dict[int, int] = {}
        for depth in sorted(levels):
            pages = levels[depth]
            level_raw = sum(target.pages[p].length for p in pages)
            if depth > 1 and sent + level_raw * sent / raw > full_sent and stop is None:
                stop, stop_sent = depth, sent + requests
            answer = make_transfer(D, target, encoder, pages, "pull")
            _, level_sent = fragments_sent(answer.stream, 3)
            request = REQUEST_HEADER + 32 * len(pages)
            costs[depth] = level_sent + request
            print(
                f"  level {depth}: {len(pages):,} pages, {level_raw:,} B raw, {level_sent:,} B as"
                f" sent, request {request:,} B; pulled so far {sent + level_sent + requests + request:,} B"
            )
            sent, raw, requests, rounds = (
                sent + level_sent,
                raw + level_raw,
                requests + request,
                rounds + 1,
            )
        pull_total = sent + requests
        check = make_transfer(D, target, encoder, pulled, "digests")
        got = receive_stream(
            D, check.stream, index, root, lambda n: D.segment_id(2_000_000 + n)
        )
        assert got.root == root
        written = sum(len(s) for s in got.segments)
        full_hash = encoder.hashed + encoder.raw
        print(
            f"  pull: {len(pulled):,} pages in {rounds} round trips, {pull_total:,} B as sent"
            f" ({requests:,} B of requests), {raw:,} B raw; a warm delta of the same pages"
            f" (wire form) {delta_sent:,} B"
        )
        print(
            f"  pull receiver: SHA-256 page input {got.hashed:,} B in {got.digests:,} digests plus"
            f" packs {written:,} B = {got.hashed + written:,} B; writes {written:,} B; the"
            f" records path's D7 hashes and writes the same pages"
        )
        print(
            f"  full copy of the head: {full_sent:,} B as sent, 1 round trip; receiver SHA-256"
            f" {full_hash:,} B, writes {encoder.raw:,} B"
        )
        print(
            f"  records answer (modeled, calibrated): {records_cal:,.0f} B as sent, 1 round trip"
        )
        if stop is None:
            ahead, ahead_total = "pull", pull_total
        else:
            ahead, ahead_total = (
                f"full copy after level {stop - 1}",
                stop_sent + full_sent,
            )
        chosen, rule_total, rule_rounds = stop_rule(
            D, target, encoder, levels, costs, full_sent
        )
        best = min(pull_total, full_sent)
        print(
            f"  one level ahead (superseded): {ahead}, {ahead_total:,} B, {ahead_total / best:.3f}"
            " of the smaller"
        )
        print(
            f"  stop rule: {chosen}, {rule_total:,} B in {rule_rounds} round trips, {rule_total / best:.3f}"
            f" of the smaller of pull and full copy ({best:,} B); pull / full copy"
            f" {pull_total / full_sent:.3f}, pull / records {pull_total / records_cal:.1f}, full"
            f" copy / records {full_sent / records_cal:.2f}; {time.monotonic() - t0:.0f} s",
            flush=True,
        )


# --- self-check ---------------------------------------------------------------------------------


def half_place(D: Any, lengths: list[int]) -> Place:
    """A forger's layout: segments broken at 512 KiB (the review's P1)."""
    out: Place = []
    number, used = 0, D.SEGMENT
    for length in lengths:
        start = (used + 7) & ~7
        if start + length > D.SEGMENT // 2:
            number, start = number + 1, 0
        out.append((number, start))
        used = start + length
    return out


def check_main(D: Any, corpus: Any) -> None:
    scale = full_main(D, corpus)
    packing, encoder = encode(D, corpus)
    root = D.state_commitment(packing)
    one = D.segment_id_from_one
    chosen = list(range(len(packing.pages)))
    assert packing.root == chosen[-1]
    built = [encoder.memory[D.segment_id(o)] for o in range(encoder.segments)]
    wire = make_transfer(D, packing, encoder, chosen, "wire")
    digests = make_transfer(D, packing, encoder, chosen, "digests")
    tail = packing.pages[packing.root].length  # the root page ends every stream

    def case(name: str, run: Any, expect: str, mutant: str | None = None) -> None:
        """Refused with every check; accepted with the mutant's check off."""
        try:
            run(frozenset())
        except Refused as error:
            why = str(error)
        else:
            raise AssertionError(f"check: {name}: accepted")
        assert expect in why, f"check: {name}: refused for {why!r}, not {expect!r}"
        line = f"check: {name}: refused ({why})"
        if mutant is not None:
            try:
                got = run(frozenset({mutant}))
            except Refused as error:
                raise AssertionError(
                    f"check: {name}: still refused without '{mutant}' ({error})"
                ) from None
            same = "identical to" if got.segments == built else "differ from"
            line += f"; mutant without '{mutant}': accepted, files {same} the build"
        print(line, flush=True)

    def stream_run(stream: bytes, expected: bytes | None) -> Any:
        return lambda skip: receive_stream(D, stream, None, expected, one, skip)

    def edit_root(stream: bytes, at: int, value: bytes) -> bytes:
        s = bytearray(stream)
        start = len(s) - tail + at
        s[start : start + len(value)] = value
        return bytes(s)

    def sealed(stream: bytes, check: str) -> tuple[bytes, bytes]:
        """A forgery sealed as its sender would seal it: the root page's stated digest (zero in
        the wire form) is recomputed, so that only `check` can refuse it."""
        stated = edit_root(stream, 32, bytes(32))
        named = receive_stream(D, stated, None, None, one, frozenset({check})).root
        if any(stream[len(stream) - tail + 32 : len(stream) - tail + 64]):
            return edit_root(stated, 32, named), named
        return stated, named

    def forgery(name: str, stream: bytes, expect: str, check: str) -> None:
        sent, named = sealed(stream, check)
        case(name, stream_run(sent, named), expect, check)

    # Damage the honest sender's bytes: the root and the stated digests find it.
    page_starts = [0]
    for p in chosen[:-1]:
        page_starts.append(page_starts[-1] + packing.pages[p].length)
    middle = next(
        p
        for p in range(len(chosen) // 2, len(chosen))
        if parse_page(
            D,
            wire.stream[page_starts[p] : page_starts[p] + packing.pages[p].length],
            frozenset(),
        )
        .nodes[-1]
        .leaf
    )
    body = (
        page_starts[middle]
        + packing.pages[middle].length
        - 68 * packing.pages[middle].externals
        - 1
    )
    for form, transfer, expect in (
        ("wire", wire, "root digest mismatch"),
        ("digests", digests, "page digest mismatch"),
    ):
        flipped = bytearray(transfer.stream)
        flipped[body] ^= 1
        case(
            f"flipped leaf body byte, {form} form",
            stream_run(bytes(flipped), root),
            expect,
        )
    root_externals = packing.pages[packing.root].externals
    redirect = bytearray(wire.stream)
    at = len(redirect) - 20 * root_externals
    redirect[at + 12 : at + 20] = (1).to_bytes(4, "big") + (0).to_bytes(4, "big")
    case(
        "root's first placement redirected to the first page",
        stream_run(bytes(redirect), root),
        "a page no parent references",
    )
    case(
        "root sent first",
        stream_run(wire.stream[-tail:] + wire.stream[:-tail], root),
        "placement names no page of the derived layout",
    )
    leaf = next(p for p, page in enumerate(packing.pages) if page.externals == 0)
    twice = make_transfer(D, packing, encoder, [leaf] + chosen, "wire")
    case(
        "an extra, unreferenced copy of a leaf",
        stream_run(twice.stream, root),
        "no parent references",
    )
    case("one byte short", stream_run(wire.stream[:-1], root), "page length")
    case(
        "root dropped",
        stream_run(wire.stream[:-tail], root),
        "a page no parent references",
    )

    # DD-1: the layout and the order are the build's, or refused.
    half = make_transfer(D, packing, encoder, chosen, "wire", place=half_place)
    case(
        "P1 segments broken at 512 KiB, handed over as segments",
        lambda skip: receive(D, half.segments, None, root, one, skip),
        "placement names no page of the derived layout",
        "layout",
    )
    case(
        "P1 placement naming 512 KiB breaks, as a stream",
        stream_run(half.stream, root),
        "placement names no page of the derived layout",
    )
    children: dict[int, list[int]] = {}
    for p, page in enumerate(packing.pages):
        children.setdefault(page.parent, []).append(p)
    reverse: list[int] = []
    stack = [(packing.root, False)]
    while stack:
        p, expanded = stack.pop()
        if expanded:
            reverse.append(p)
            continue
        stack.append((p, True))
        stack.extend((c, False) for c in children.get(p, []))
    assert sorted(reverse) == chosen and reverse != chosen
    other = make_transfer(D, packing, encoder, reverse, "wire")
    case(
        "P2 children first, not in seal order",
        stream_run(other.stream, root),
        "seal order",
        "order",
    )

    # DD-2: a tree the sender forged under a root it names, each forgery validly sealed so that
    # only its targeted check refuses it.
    keys = D.be(wire.stream, len(wire.stream) - tail + 16, 8)
    for form, transfer in (("wire", wire), ("digests", digests)):
        forgery(
            f"P4 root header keys +1 under a forged root, {form} form",
            edit_root(transfer.stream, 16, (keys + 1).to_bytes(8, "big")),
            "header keys disagree with the page",
            "keys",
        )
    height = wire.stream[len(wire.stream) - tail + 24]
    forgery(
        "root header height +1 under a forged root",
        edit_root(wire.stream, 24, bytes((height + 1,))),
        "header height disagrees with the page",
        "height",
    )
    top = len(wire.stream) - tail + 64
    assert wire.stream[top] == 1 and wire.stream[top + 2] >= 1
    labels = top + 3 + wire.stream[top + 1]
    forgery(
        "root's second label equal to its first, under a forged root",
        edit_root(
            wire.stream,
            labels - len(wire.stream) + tail + 1,
            wire.stream[labels : labels + 1],
        ),
        "branch labels",
        "labels",
    )
    count = D.be(wire.stream, len(wire.stream) - tail + 14, 2)
    forgery(
        "root header node count +1, under a forged root",
        edit_root(wire.stream, 14, (count + 1).to_bytes(2, "big")),
        "page structure: nodes do not fill",
        "parse",
    )
    largest_encoder = D.Encoder(memory=True, compress=False)
    largest = D.pack_dense(corpus, CAPACITY, "largest", largest_encoder)
    largest_root = D.state_commitment(largest)
    assert largest_root != root
    everything = list(range(len(largest.pages)))
    shape = receive_stream(
        D,
        make_transfer(D, largest, largest_encoder, everything, "wire").stream,
        None,
        largest_root,
        one,
        frozenset({"order", "canonical"}),
    )
    forged_tree = make_transfer(D, largest, largest_encoder, shape.order, "wire")
    case(
        f"the tree packed largest-first ({len(largest.pages):,} pages against {len(chosen):,}), in"
        " the order its pages imply, under its own root",
        stream_run(forged_tree.stream, largest_root),
        "not packed by D5",
        "canonical",
    )

    # DD-7: zero-page transfers.
    index = base_index(D, packing, encoder)
    assert receive_stream(D, b"", None, D.EMPTY_COMMITMENT, one).segments == []
    assert receive_stream(D, b"", index, root, one).segments == []
    print(
        "check: empty stream publishes the empty commitment; over a base at the target root it writes nothing"
    )
    case(
        "empty stream over a base at another root",
        lambda skip: receive_stream(D, b"", index, D.EMPTY_COMMITMENT, one, skip),
        "empty transfer",
    )

    definitions = sum(
        1 for i in range(corpus.count) if corpus.kinds[i] == D.KIND_DEFINITION
    )
    delta_main(
        D,
        corpus,
        ["u:1", f"u:{definitions // 50}", f"c:{definitions // 50}", "m:98:1:1"],
        scale,
    )
    stale_main(D, corpus, ["1:moderate", "24:hotrange", "168:uniform"], scale)
    print("check: ok")


def main(argv: list[str]) -> None:
    global D
    options = {
        a[2:].partition("=")[0]: a[2:].partition("=")[2]
        for a in argv
        if a.startswith("--")
    }
    rest = [a for a in argv[1:] if not a.startswith("--")]
    if len(rest) < 2:
        raise SystemExit(__doc__)
    default = (
        Path(os.environ.get("JOB_REF_DENSE", ".")) / "docs/design/dense-pages-model.py"
    )
    D = load_dense(Path(options.get("dense", default)))
    D.segment_id_from_one = lambda n: D.segment_id(n - 1)
    print(
        f"python {sys.version.split()[0]}; corpus {rest[1]} sha256 {hashlib.sha256(Path(rest[1]).read_bytes()).hexdigest()}"
    )
    corpus = D.read_corpus(Path(rest[1]))
    print(f"corpus: {corpus.count:,} records", flush=True)
    mode = rest[0]
    if mode == "full":
        full_main(D, corpus)
    elif mode == "delta":
        delta_main(D, corpus, rest[2:], calibrate(D, corpus))
    elif mode == "day":
        day_main(D, corpus, calibrate(D, corpus))
    elif mode == "stale":
        stale_main(D, corpus, rest[2:], calibrate(D, corpus))
    elif mode == "check":
        check_main(D, corpus)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
