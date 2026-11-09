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
  check CORPUS              Both modes on a small corpus, plus refusal cases: a flipped body byte,
                            a redirected placement and a parent sent before its child.

Options: --dense=PATH (the dense model; default $JOB_REF_DENSE/docs/design/dense-pages-model.py).
Needs Python 3.14 (compression.zstd). Run under a light hold, as a campaign job, e.g.:
  campaign job submit --quest dense-distribution --ref DENSE=28d3ed9 --level light --minutes 60 \\
      -- /usr/local/bin/python3 -I docs/design/dense-distribution-model.py full CORPUS
"""

from __future__ import annotations

import hashlib
import importlib.util
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
# A transfer lists the pages a receiver lacks in the target's seal order (children first), laid
# out as the receiver will write them: 8-byte offsets in 1 MiB segments numbered from 1. Its page
# bytes are the stored bytes with the fields the receiver derives rewritten:
#   "stored"  the bytes as the sender holds them (placement in the sender's segment ids);
#   "digests" placement of an in-transfer child is (transfer segment, offset), of a base child zero;
#             every digest kept, so a bad page is found where it is;
#   "wire"    as "digests", with the header digest and every in-transfer child's digest zeroed:
#             the receiver computes them bottom-up and checks only the root.

FORMS = ("stored", "digests", "wire")


@dataclass(slots=True)
class Transfer:
    segments: list[bytes]
    pages: int
    page_bytes: int


def transfer_place(D: Any, lengths: list[int]) -> list[tuple[int, int]]:
    """(segment number from 1, offset) of each page, laid out as the encoder lays out a build."""
    out: list[tuple[int, int]] = []
    number, used = 0, D.SEGMENT
    for length in lengths:
        start = (used + 7) & ~7
        if start + length > D.SEGMENT:
            number, start = number + 1, 0
        out.append((number, start))
        used = start + length
    return out


def make_transfer(
    D: Any, packing: Any, encoder: Any, chosen: list[int], form: str
) -> Transfer:
    memory = encoder.memory
    at_place = {
        (D.segment_id(o), off): page for page, (o, off) in encoder.placed.items()
    }
    lengths = [packing.pages[p].length for p in chosen]
    places = transfer_place(D, lengths)
    where = dict(zip(chosen, places, strict=True))
    segments: list[bytearray] = []
    for p, (number, offset) in zip(chosen, places, strict=True):
        o, off = encoder.placed[p]
        length = packing.pages[p].length
        page = bytearray(memory[D.segment_id(o)][off : off + length])
        if form != "stored":
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
        while len(segments) < number:
            segments.append(bytearray())
        segment = segments[number - 1]
        segment.extend(bytes(offset - len(segment)))
        segment.extend(page)
    return Transfer([bytes(s) for s in segments], len(chosen), sum(lengths))


def report_transfer(name: str, transfer: Transfer) -> dict[str, int]:
    stream = b"".join(transfer.segments)
    row = {
        "raw": len(stream),
        "seg_z1": sum(zstd_size(s, 1) for s in transfer.segments),
        "seg_z3": sum(zstd_size(s, 3) for s in transfer.segments),
        "seg_z9": sum(zstd_size(s, 9) for s in transfer.segments),
    }
    row["frags"], row["frag_z3"] = fragments_sent(stream, 3)
    print(
        f"  {name:>7}: {transfer.pages:,} pages, {transfer.page_bytes:,} page B, {row['raw']:,} B in"
        f" {len(transfer.segments):,} segments; zstd per segment -1 {row['seg_z1']:,} -3 {row['seg_z3']:,}"
        f" -9 {row['seg_z9']:,}; zstd-3 per 64 KiB fragment, headers included, {row['frag_z3']:,}"
        f" ({row['frags']:,} fragments)",
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


def receive(
    D: Any,
    segments: list[bytes],
    base: Base | None,
    expected_root: bytes,
    own_id: Any,
) -> Received:
    """Rebuilds the target's segments from a transfer in any form but "stored": placement of
    in-transfer children rewritten to own_id(n), base children resolved by path, every digest
    computed bottom-up in transfer order, and the root compared. Refuses on any inconsistency."""
    pages: list[tuple[int, int, bytearray]] = []
    out = [bytearray(s) for s in segments]
    for n, segment in enumerate(out, start=1):
        at = 0
        while at < len(segment):
            if segment[at : at + 8] != b"DTPAGE03":
                raise Refused("bytes outside a page")
            length = D.be(segment, at + 8, 4)
            if not 79 <= length <= D.MAX_PAGE or at + length > len(segment):
                raise Refused("page length")
            pages.append((n, at, memoryview(segment)[at : at + length]))  # type: ignore[arg-type]
            end = at + length
            at = min(len(segment), (end + 7) & ~7)
            if any(segment[end:at]):
                raise Refused("nonzero padding")
    if not pages:
        raise Refused("empty transfer")
    index = {(n, at): i for i, (n, at, _) in enumerate(pages)}
    # Pass 1, only with a base: paths top-down from the root (the last page), to resolve base
    # children by the path the target's own pages give them.
    resolved: dict[tuple[int, int], tuple[bytes, int, int, int, bytes, int, int]] = {}
    walk = 0
    if base is not None:
        stack = [(len(pages) - 1, b"")]
        while stack:
            i, top = stack.pop()
            page = pages[i][2]
            length, externals = len(page), D.be(page, 12, 2)
            placement = length - 20 * externals
            for j, path in enumerate(external_paths(page, top)):
                at = placement + 20 * j
                tn, toff = D.be(page, at + 12, 4), D.be(page, at + 16, 4)
                if any(page[at : at + 12]):
                    raise Refused("placement names no transfer segment")
                if tn:
                    child = index.get((tn, toff))
                    if child is None or child >= i:
                        raise Refused("child not before its parent")
                    stack.append((child, path))
                else:
                    found = base.by_path.get(path)
                    if found is None:
                        raise Refused("base lacks a page the delta reuses")
                    resolved[(i, j)] = found
                    walk += found[6]
    # Pass 2, bottom-up in transfer order.
    done: dict[int, tuple[bytes, int, int, int]] = {}
    used: set[int] = set()
    hashed = 0
    for i, (n, at0, page) in enumerate(pages):
        length, externals = len(page), D.be(page, 12, 2)
        content, placement = length - 68 * externals, length - 20 * externals
        for j in range(externals):
            entry, at = content + 48 * j, placement + 20 * j
            keys, clen, height = (
                D.be(page, entry + 32, 8),
                D.be(page, entry + 40, 4),
                page[entry + 44],
            )
            if (i, j) in resolved:
                digest, bkeys, blen, bheight, sid, boff, _ = resolved[(i, j)]
                summary = (digest, bkeys, blen, bheight)
                page[at : at + 20] = sid + boff.to_bytes(4, "big")
            else:
                tn, toff = D.be(page, at + 12, 4), D.be(page, at + 16, 4)
                child = index.get((tn, toff))
                if any(page[at : at + 12]) or child is None or child not in done:
                    raise Refused("child not before its parent")
                if child in used:
                    raise Refused("page referenced twice")
                used.add(child)
                summary = done[child]
                page[at : at + 20] = own_id(tn) + toff.to_bytes(4, "big")
            stated = bytes(page[entry : entry + 32])
            if stated != bytes(32) and stated != summary[0]:
                raise Refused("content digest mismatch")
            if (keys, clen, height) != summary[1:]:
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
        done[i] = (digest, D.be(page, 16, 8), length, page[24])
    root = len(pages) - 1
    if len(used) != root:
        raise Refused("a page no parent references")
    if done[root][0] != expected_root:
        raise Refused("root digest mismatch")
    walks = sum(1 for _ in resolved)
    return Received(
        [bytes(s) for s in out], done[root][0], hashed, len(pages), walks, walk
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
        got = receive(D, wire.segments, None, root, D.segment_id_from_one)
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
) -> None:
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
        got = receive(
            D, wire.segments, index, root, lambda n: D.segment_id(1_000_000 + n)
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
        print(f"  {time.monotonic() - t0:.0f} s", flush=True)


# --- self-check ---------------------------------------------------------------------------------


def check_main(D: Any, corpus: Any) -> None:
    scale = full_main(D, corpus)
    packing, encoder = encode(D, corpus)
    root = D.state_commitment(packing)
    chosen = list(range(len(packing.pages)))
    wire = make_transfer(D, packing, encoder, chosen, "wire")
    digests = make_transfer(D, packing, encoder, chosen, "digests")

    def refused(segments: list[bytes], form: str) -> str:
        try:
            receive(D, segments, None, root, D.segment_id_from_one)
        except Refused as error:
            return str(error)
        raise AssertionError(f"accepted a damaged {form} transfer")

    middle = len(wire.segments) // 2
    flipped = bytearray(wire.segments[middle])
    flipped[200] ^= 1  # a node byte of the segment's first page
    print(
        f"check: flipped byte, wire form: {refused(wire.segments[:middle] + [bytes(flipped)] + wire.segments[middle + 1 :], 'wire')}"
    )
    flipped = bytearray(digests.segments[middle])
    flipped[200] ^= 1
    print(
        f"check: flipped byte, digests form: {refused(digests.segments[:middle] + [bytes(flipped)] + digests.segments[middle + 1 :], 'digests')}"
    )
    # Redirect the root's first external to another page of the transfer.
    last = bytearray(wire.segments[-1])
    at = last.rfind(b"DTPAGE03")
    length, externals = D.be(last, at + 8, 4), D.be(last, at + 12, 2)
    entry = at + length - 20 * externals
    last[entry + 12 : entry + 16] = (1).to_bytes(4, "big")
    last[entry + 16 : entry + 20] = (0).to_bytes(4, "big")
    print(
        f"check: redirected placement: {refused(wire.segments[:-1] + [bytes(last)], 'wire')}"
    )
    # A parent before its child: the root moved to the front of a one-segment transfer.
    if len(wire.segments) == 1:
        print("check: one segment; reorder case skipped")
    else:
        print(
            f"check: parent first: {refused([wire.segments[-1]] + wire.segments[:-1], 'wire')}"
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
    elif mode == "check":
        check_main(D, corpus)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
