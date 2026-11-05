"""Dense paged-trie model: today's one node per page against dense pages, on a real store.

Status: DRAFT (2026-11-04). The numbers behind docs/design/dense-pages.md. Byte and page counts
are exact for the corpus and for the two formats as that document describes them; no time here is
measured. SHA-256 times are estimates from the review's sha_page numbers (SHA_FIXED_US below).

Modes:
  extract STORE CORPUS      Walk a DTPAGE02 store's trie from its root image, check the page-byte
                            split against the root, compress every pack with zstd 1 and 3, and
                            write the keys and bodies in key order.
  today CORPUS              Rebuild today's trie from the corpus: pages, bytes, SHA-256 bytes at
                            build, pages and first-touch bytes per lookup.
  pack CORPUS [SIZE...]     Dense packing per page capacity and sealing rule (--rule=a,b,c):
                            pages, bytes, pages and first-touch bytes per lookup, SHA-256 bytes.
  bytes CORPUS SIZE [OUT]   Encode the dense pages at one capacity into 1 MiB segments and
                            compress each with zstd 1 and 3; OUT keeps the segment files.
  delta CORPUS [SIZE...]    Bytes written and hashed per delta of 1, 100 and 10,000 changed keys,
                            today against dense, and the share of the generation reused.
  churn CORPUS SIZE [U I D] U updates, I inserts, D deletes: both full builds diffed by page
                            digest, each written page classed as on a changed path or not;
                            --out=PATH writes the changed corpus.
  verify CORPUS SIZE [N]    Encode in memory and look N keys up through the encoded bytes with
                            first-touch digests and hit compares, as a reader would.
  synthetic OUT             Write the synthetic corpus: four namespaces of 5,000 definitions,
                            and one whose 200 leaves share a branch, a page above 4 KiB.
  check DIR                 The model's self-check on the synthetic corpus, writing two corpora
                            in DIR: every mode runs, keys round-trip, builds are identical.

Options after the mode's arguments: --rule=largest,density,height (sealing rules, D5);
--namespace=NAME (that namespace alone); --shape=hex (local names replaced by hashed hex).

Needs Python 3.14 (compression.zstd). Run from the repository root under a light hold, e.g.:
  python3 -I docs/design/dense-pages-model.py extract \\
      .scratch/capacity-trials/5000000-restore-r12/agent-0/generations .scratch/dense/corpus-5m.bin
  python3 -I docs/design/dense-pages-model.py pack .scratch/dense/corpus-5m.bin 4096 16384
"""

from __future__ import annotations

import hashlib
import itertools
import mmap
import random
import struct
import sys
import time
from array import array
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path

# --- today's format (include/detent_local/paged_layout.hh:7-8; paged_layout.cc:130-169) -----

HEADER_V2, CHILD_V2 = 80, 96
LOGICAL_FIXED, LOGICAL_CHILD = (
    44,
    33,
)  # logical commitment input: domain and lengths; label, digest
SEGMENT = 1024 * 1024
KIND_BRANCH, KIND_DEFINITION, KIND_NAMESPACE = 1, 2, 3

# --- dense format (docs/design/dense-pages.md D2, D3) ---------------------------------------

HEADER = (
    64  # magic 8, length 4, externals 2, nodes 2, keys 8, height 1, zero 7, digest 32
)
CONTENT = (
    48  # an external's content entry: digest 32, keys 8, length 4, height 1, zero 3
)
PLACEMENT = (
    20  # an external's placement entry: segment 16, offset 4 (outside the digest)
)
EXTERNAL = CONTENT + PLACEMENT
MAX_PAGE = 64 * 1024
# Node tags are today's PageKind values (paged_layout.hh:9); zero is never a tag.
TAG_BRANCH, TAG_DEFINITION, TAG_NAMESPACE = 1, 2, 3

# docs/reviews/reader-design-preliminary.md:54: two SHA-256 passes over a page took 2.2 us at
# 200 B, 35 us at 4 KiB and 0.54 ms at 64 KiB (Kronuz/sha256, software only), so one pass is
# about 0.3 us plus 243 bytes per us. An estimate, not a measurement made here.
SHA_FIXED_US, SHA_BYTES_PER_US = 0.3, 243.0


def sha_us(nbytes: float, digests: float = 1.0) -> float:
    return digests * SHA_FIXED_US + nbytes / SHA_BYTES_PER_US


def be(data: bytes | memoryview | mmap.mmap, at: int, width: int) -> int:
    return int.from_bytes(data[at : at + width], "big")


def quantile(sorted_values: Sequence[int], share: float) -> int:
    if not sorted_values:
        return 0
    return sorted_values[min(len(sorted_values) - 1, int(share * len(sorted_values)))]


def histogram_quantile(counts: dict[int, int], share: float) -> int:
    total = sum(counts.values())
    seen = 0
    for value in sorted(counts):
        seen += counts[value]
        if seen > share * total:
            return value
    return max(counts) if counts else 0


def mean(values: Sequence[int] | Sequence[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def segments_for(lengths: Sequence[int]) -> tuple[int, int]:
    """Pages appended in order to 1 MiB segments at 8-byte offsets: (segments, bytes)."""
    segments, used, total = 0, SEGMENT, 0
    for length in lengths:
        start = (used + 7) & ~7
        if start + length > SEGMENT:
            total += used if segments else 0
            segments += 1
            start = 0
        used = start + length
    total += used if segments else 0
    return segments, total


# --- corpus -----------------------------------------------------------------------------------

MAGIC = b"DTCORP01"


@dataclass(slots=True)
class Corpus:
    """Keys in ascending byte order with their kinds and exact bodies; watermark[i] is the
    ordinal of the W key of record i's namespace."""

    keys: bytes
    key_offsets: array[int]
    bodies: bytes
    body_offsets: array[int]
    kinds: bytes
    watermark: array[int]

    @property
    def count(self) -> int:
        return len(self.kinds)

    def key(self, i: int) -> bytes:
        return self.keys[self.key_offsets[i] : self.key_offsets[i + 1]]

    def body(self, i: int) -> bytes:
        return self.bodies[self.body_offsets[i] : self.body_offsets[i + 1]]

    def body_length(self, i: int) -> int:
        return self.body_offsets[i + 1] - self.body_offsets[i]


class CorpusWriter:
    def __init__(self) -> None:
        self.keys, self.bodies, self.kinds = bytearray(), bytearray(), bytearray()
        self.key_offsets, self.body_offsets = array("I", [0]), array("I", [0])

    def add(self, key: bytes, kind: int, body: bytes) -> None:
        self.keys += key
        self.bodies += body
        self.kinds.append(kind)
        self.key_offsets.append(len(self.keys))
        self.body_offsets.append(len(self.bodies))

    def corpus(self) -> Corpus:
        keys, kinds = bytes(self.keys), bytes(self.kinds)
        marks = watermarks(keys, self.key_offsets, kinds)
        return Corpus(
            keys, self.key_offsets, bytes(self.bodies), self.body_offsets, kinds, marks
        )

    def write(self, path: Path) -> None:
        key_offsets, body_offsets = (
            array("I", self.key_offsets),
            array("I", self.body_offsets),
        )
        key_offsets.byteswap()
        body_offsets.byteswap()
        with path.open("wb") as out:
            out.write(
                MAGIC
                + struct.pack(">QQQ", len(self.kinds), len(self.keys), len(self.bodies))
            )
            for part in (
                key_offsets.tobytes(),
                self.keys,
                body_offsets.tobytes(),
                self.bodies,
                self.kinds,
            ):
                out.write(part)


def watermarks(keys: bytes, key_offsets: array[int], kinds: bytes) -> array[int]:
    count = len(kinds)
    names: dict[bytes, int] = {}
    for i in range(count):
        if kinds[i] == KIND_NAMESPACE:
            names[keys[key_offsets[i] + 1 : key_offsets[i + 1] - 1]] = i
    result = array("I", bytes(4 * count))
    scope, owner = b"", 0
    for i in range(count):
        key = keys[key_offsets[i] : key_offsets[i + 1]]
        if not scope or not key.startswith(scope):
            name = key[1 : key.index(0, 1)]
            scope, owner = key[: len(name) + 2], names[name]
        result[i] = owner
    return result


def read_corpus(path: Path) -> Corpus:
    data = path.read_bytes()
    if data[:8] != MAGIC:
        raise SystemExit(f"{path}: not a corpus file")
    count, key_bytes, body_bytes = struct.unpack(">QQQ", data[8:32])
    at = 32
    key_offsets = array("I")
    key_offsets.frombytes(data[at : at + 4 * (count + 1)])
    key_offsets.byteswap()
    at += 4 * (count + 1)
    keys = data[at : at + key_bytes]
    at += key_bytes
    body_offsets = array("I")
    body_offsets.frombytes(data[at : at + 4 * (count + 1)])
    body_offsets.byteswap()
    at += 4 * (count + 1)
    bodies = data[at : at + body_bytes]
    kinds = data[at + body_bytes : at + body_bytes + count]
    return Corpus(
        keys,
        key_offsets,
        bodies,
        body_offsets,
        kinds,
        watermarks(keys, key_offsets, kinds),
    )


def reshape(corpus: Corpus, namespace: bytes | None, shape: str) -> Corpus:
    """Sensitivity variants: one namespace alone (namespace-delivery D8 keeps one generation per
    namespace), and local names replaced by 16 hex digits of their hash, so the trie branches 16
    ways at random instead of 10 or 11 ways over decimal counters."""
    if namespace is None and shape == "store":
        return corpus
    records: list[tuple[bytes, int, bytes]] = []
    for i in range(corpus.count):
        key = corpus.key(i)
        name = key[1 : key.index(0, 1)]
        if namespace is not None and name != namespace:
            continue
        if shape == "hex" and corpus.kinds[i] == KIND_DEFINITION:
            local = key[len(name) + 2 : -1]
            key = (
                key[: len(name) + 2]
                + hashlib.blake2b(local, digest_size=8).hexdigest().encode()
                + b"\0"
            )
        elif shape not in ("store", "hex"):
            raise SystemExit(f"unknown shape {shape}")
        records.append((key, corpus.kinds[i], corpus.body(i)))
    records.sort()
    assert all(records[i][0] != records[i + 1][0] for i in range(len(records) - 1)), (
        "hashed names collide"
    )
    writer = CorpusWriter()
    for key, kind, body in records:
        writer.add(key, kind, body)
    return writer.corpus()


def extract_main(store: Path, out: Path) -> None:
    """Walks the trie depth first in label order, so every page is read once and keys come out
    sorted; compresses each pack whole, as a shipped file would be."""
    from compression import zstd

    started = time.monotonic()
    image_path = next(store.glob("generation-*.image"))
    image = image_path.read_bytes()
    if image[:8] != b"DTROOT02":
        raise SystemExit("not a DTROOT02 root")
    root_bytes, root_records, root_namespaces = (
        be(image, 16, 8),
        be(image, 24, 4),
        be(image, 28, 4),
    )
    root_pages, root_segments = be(image, 32, 8), be(image, 40, 8)
    maps: dict[bytes, mmap.mmap] = {}
    pack_bytes = zstd1 = zstd3 = 0
    for pack in sorted(store.glob("segment-*.pack")):
        data = pack.read_bytes()
        pack_bytes += len(data)
        zstd1 += len(zstd.compress(data, level=1))
        zstd3 += len(zstd.compress(data, level=3))
        with pack.open("rb") as handle:
            maps[bytes.fromhex(pack.name[8:40])] = mmap.mmap(
                handle.fileno(), 0, access=mmap.ACCESS_READ
            )
    split = {
        "pages": 0,
        "header": 0,
        "prefix": 0,
        "children": 0,
        "child_count": 0,
        "body": 0,
        "branches": 0,
    }
    writer = CorpusWriter()
    previous = b""
    stack: list[tuple[bytes, bytes]] = [(image[160:248], b"")]
    while stack:
        reference, key = stack.pop()
        segment = maps[reference[:16]]
        offset, length = be(reference, 16, 4), be(reference, 20, 4)
        page = segment[offset : offset + length]
        kind, prefix = be(page, 12, 4), be(page, 20, 4)
        children, body = be(page, 24, 4), be(page, 28, 4)
        assert (
            page[:8] == b"DTPAGE02"
            and length == HEADER_V2 + prefix + CHILD_V2 * children + body
        )
        split["pages"] += 1
        split["header"] += HEADER_V2
        split["prefix"] += prefix
        split["children"] += CHILD_V2 * children
        split["child_count"] += children
        split["body"] += body
        key += page[HEADER_V2 : HEADER_V2 + prefix]
        if kind == KIND_BRANCH:
            split["branches"] += 1
            base = HEADER_V2 + prefix
            for i in reversed(range(children)):
                child = page[base + CHILD_V2 * i : base + CHILD_V2 * (i + 1)]
                stack.append((child[8:96], key + child[:1]))
        else:
            assert key > previous, "keys out of order"
            previous = key
            writer.add(key, kind, page[length - body :])
    page_bytes = split["header"] + split["prefix"] + split["children"] + split["body"]
    print(
        f"root {image_path.name}: records {root_records:,}, namespaces {root_namespaces:,},"
    )
    print(
        f"  pages {root_pages:,}, segments {root_segments:,}, page bytes {root_bytes:,}"
    )
    print(
        f"walked: pages {split['pages']:,} ({split['branches']:,} branches), child references"
    )
    print(
        f"  {split['child_count']:,}, page bytes {page_bytes:,} ({page_bytes / max(1, root_records):.1f} B per record)"
    )
    for name in ("header", "children", "prefix", "body"):
        print(f"  {name:9s} {split[name]:>15,d} {100 * split[name] / page_bytes:6.2f}%")
    print(
        f"packs: {len(maps):,} files, {pack_bytes:,} B; zstd-1 {zstd1:,} B ({100 * zstd1 / pack_bytes:.1f}%),"
    )
    print(
        f"  zstd-3 {zstd3:,} B ({100 * zstd3 / pack_bytes:.1f}%); framing and padding {pack_bytes - page_bytes:,} B"
    )
    if (
        split["pages"] != root_pages
        or len(maps) != root_segments
        or page_bytes != root_bytes
    ):
        print("WARNING: the walk does not match the root's counts")
    writer.write(out)
    print(
        f"wrote {len(writer.kinds):,} records to {out} in {time.monotonic() - started:.0f} s"
    )


# --- the canonical trie, built from sorted keys --------------------------------------------
# The compressed byte trie TrieCursor emits (paged_trie_cursor.cc): a branch where neighbours
# first differ, a leaf's suffix after its parent's label, postorder. Keys end in NUL, so no key
# is a prefix of another.


def common_prefix(a: bytes, b: bytes) -> int:
    m = min(len(a), len(b))
    x = int.from_bytes(a[:m], "big") ^ int.from_bytes(b[:m], "big")
    return m if x == 0 else m - (x.bit_length() + 7) // 8


LeafFn = Callable[[int, bytes, int], object]
BranchFn = Callable[[bytes, int, int, list[tuple[int, object]]], object]


def build_trie(
    count: int, key_at: Callable[[int], bytes], on_leaf: LeafFn, on_branch: BranchFn
) -> object:
    """Calls on_leaf(i, key, suffix_start) and on_branch(key, prefix_start, depth, children)
    in postorder; depth is the branching byte's index and children is [(label, summary)] in
    label order. Returns the root's summary."""
    frames: list[tuple[int, list[tuple[int, object]]]] = []
    root: object = None
    previous = key_at(0)
    for i in range(count):
        following = key_at(i + 1) if i + 1 < count else b""
        common = common_prefix(previous, following) if following else -1
        if common > (frames[-1][0] if frames else -1):
            frames.append((common, []))
        summary = on_leaf(i, previous, (frames[-1][0] if frames else -1) + 1)
        if not frames:
            root = summary
        else:
            frames[-1][1].append((previous[frames[-1][0]], summary))
            while frames and frames[-1][0] > common:
                depth, children = frames.pop()
                below = frames[-1][0] if frames else -1
                summary = on_branch(previous, max(common, below) + 1, depth, children)
                if common > below:
                    frames.append((common, []))
                if frames:
                    frames[-1][1].append((previous[frames[-1][0]], summary))
                else:
                    root = summary
        previous = following
    return root


# --- today: one node per page ----------------------------------------------------------------


@dataclass(slots=True)
class TodayTrie:
    """Nodes in postorder: size is the page length, verify the bytes a first touch hashes
    (physical plus logical input), parent the parent's id (the root's is its own)."""

    size: array[int] = field(default_factory=lambda: array("I"))
    verify: array[int] = field(default_factory=lambda: array("I"))
    parent: array[int] = field(default_factory=lambda: array("I"))
    leaf_node: array[int] = field(default_factory=lambda: array("I"))
    logical: list[bytes] = field(default_factory=list)
    prefix_bytes: int = 0
    child_count: int = 0
    branches: int = 0
    root_labels: bytes = b""


def build_today(corpus: Corpus, digests: bool = False) -> TodayTrie:
    trie = TodayTrie()
    size, verify, parent, leaf_node, logical = (
        trie.size,
        trie.verify,
        trie.parent,
        trie.leaf_node,
        trie.logical,
    )

    def on_leaf(i: int, key: bytes, start: int) -> int:
        node = len(size)
        suffix, body = len(key) - start, corpus.body_length(i)
        size.append(HEADER_V2 + suffix + body)
        verify.append(HEADER_V2 + suffix + body + LOGICAL_FIXED + suffix + body)
        parent.append(node)
        leaf_node.append(node)
        trie.prefix_bytes += suffix
        if digests:
            content = bytes([corpus.kinds[i]]) + key[start:] + b"\0" + corpus.body(i)
            logical.append(hashlib.blake2b(content, digest_size=16).digest())
        return node

    def on_branch(
        key: bytes, start: int, depth: int, children: list[tuple[int, object]]
    ) -> int:
        node = len(size)
        prefix, count = depth - start, len(children)
        size.append(HEADER_V2 + prefix + CHILD_V2 * count)
        verify.append(
            HEADER_V2
            + prefix
            + CHILD_V2 * count
            + LOGICAL_FIXED
            + prefix
            + LOGICAL_CHILD * count
        )
        parent.append(node)
        for _, child in children:
            assert isinstance(child, int)
            parent[child] = node
        trie.prefix_bytes += prefix
        trie.child_count += count
        trie.branches += 1
        if start == 0:
            trie.root_labels = bytes(label for label, _ in children)
        if digests:
            h = hashlib.blake2b(b"B" + key[start:depth] + b"\0", digest_size=16)
            for label, child in children:
                assert isinstance(child, int)
                h.update(bytes([label]) + logical[child])
            logical.append(h.digest())
        return node

    build_trie(corpus.count, corpus.key, on_leaf, on_branch)
    return trie


def today_paths(
    corpus: Corpus, trie: TodayTrie
) -> tuple[dict[int, int], dict[int, int], list[int], list[int]]:
    """Pages per definition walk, per definition and watermark walk, the bytes a lookup hashes
    when every page on both walks is cold, and the bytes the first touch of the definition's own
    page hashes. K and W differ at byte 0, so both walks share exactly the root."""
    n = len(trie.size)
    assert trie.root_labels == b"KW", trie.root_labels
    depth = array("H", bytes(2 * n))
    path = array("Q", bytes(8 * n))
    for node in range(n - 1, -1, -1):
        up = trie.parent[node]
        if up == node:
            depth[node], path[node] = 1, trie.verify[node]
        else:
            depth[node], path[node] = depth[up] + 1, path[up] + trie.verify[node]
    walk: dict[int, int] = {}
    both: dict[int, int] = {}
    touch = array("Q")
    bottom = array("Q")
    root_verify = trie.verify[n - 1]
    for i in range(corpus.count):
        if corpus.kinds[i] != KIND_DEFINITION:
            continue
        leaf, w = trie.leaf_node[i], trie.leaf_node[corpus.watermark[i]]
        walk[depth[leaf]] = walk.get(depth[leaf], 0) + 1
        pages = depth[leaf] + depth[w] - 1
        both[pages] = both.get(pages, 0) + 1
        touch.append(path[leaf] + path[w] - root_verify)
        bottom.append(trie.verify[leaf])
    return walk, both, sorted(touch), sorted(bottom)


def today_main(corpus: Corpus) -> None:
    started = time.monotonic()
    trie = build_today(corpus)
    pages, total = len(trie.size), sum(trie.size)
    segments, segment_bytes = segments_for(trie.size)
    logical = sum(trie.verify) - total
    print(
        f"today: {pages:,} pages ({trie.branches:,} branches), {trie.child_count:,} child references,"
    )
    print(
        f"  prefixes {trie.prefix_bytes:,} B, page bytes {total:,} ({total / corpus.count:.1f} B per record)"
    )
    print(f"  segments in postorder: {segments:,}, {segment_bytes:,} B")
    hashed = total + logical + segment_bytes
    print(
        f"  SHA-256 at build: physical {total:,} + logical {logical:,} + segment checksums"
    )
    print(f"  {segment_bytes:,} = {hashed:,} B in {2 * pages + segments:,} digests")
    print(
        f"  (estimate {sha_us(hashed, 2 * pages + segments) / 1e6:.1f} s of one core)"
    )
    walk, both, touch, bottom = today_paths(corpus, trie)
    w50, w99 = histogram_quantile(walk, 0.5), histogram_quantile(walk, 0.99)
    b50, b99 = histogram_quantile(both, 0.5), histogram_quantile(both, 0.99)
    t50, t99 = quantile(touch, 0.5), quantile(touch, 0.99)
    f50, f99 = quantile(bottom, 0.5), quantile(bottom, 0.99)
    print(f"  pages per definition walk: median {w50}, p99 {w99}, max {max(walk)}")
    print(
        f"  pages per lookup (definition and watermark walks): median {b50}, p99 {b99}, max {max(both)}"
    )
    print(
        f"  cold lookup hashes: median {t50:,} B, p99 {t99:,} B (estimate {sha_us(t50, 2 * b50):.0f} us"
    )
    print(f"  and {sha_us(t99, 2 * b99):.0f} us)")
    print(
        f"  definition page first touch: median {f50:,} B, p99 {f99:,} B (estimate {sha_us(f50, 2):.1f} us"
    )
    print(
        f"  and {sha_us(f99, 2):.1f} us); built in {time.monotonic() - started:.0f} s"
    )


# --- dense pages -------------------------------------------------------------------------------
# A page holds a connected piece of the trie: a top node and the descendants kept with it, in
# preorder. A slot holds an in-page offset or an index into the page's external table. A cluster
# is the open part of a subtree not yet sealed into a page: a node plus the clusters of the
# children it keeps. Sealing runs bottom-up and reads only sizes, keys and heights below the node,
# so the page holding a node is a function of the node's subtree: canonical, free of history.


@dataclass(slots=True)
class Page:
    length: int
    keys: int
    height: int  # pages on the longest path down from this page, itself included
    externals: int
    parent: int = -1
    digest: bytes = b""


@dataclass(slots=True)
class Cluster:
    size: int  # bytes it adds to a page: its nodes and its external entries
    keys: int  # leaves under its top node, sealed pages below included
    height: int  # pages below it on the longest path
    ranges: list[list[int]]  # leaf ordinals it holds, as [lo, hi) runs
    externals: list[int]  # pages it references, in preorder
    node: object = None  # node record, kept only when encoding


@dataclass(slots=True)
class Sealed:
    page: int


@dataclass(slots=True)
class DensePacking:
    capacity: int
    rule: str
    pages: list[Page] = field(default_factory=list)
    leaf_runs: list[tuple[int, int, int]] = field(default_factory=list)  # lo, hi, page
    node_bytes: int = 0
    nodes: int = 0
    root: int = -1

    def slot_width(self) -> int:
        return 2 if self.capacity <= 32 * 1024 else 3

    def page_of_leaves(self, count: int) -> array[int]:
        result = array("I", bytes(4 * count))
        for lo, hi, page in self.leaf_runs:
            result[lo:hi] = array("I", [page]) * (hi - lo)
        return result


def choose(rule: str, items: list[Cluster | Sealed], candidates: list[int]) -> int:
    """The child to seal next, among clusters larger than an external entry. Deterministic: ties
    go to the lowest label."""

    def cluster(j: int) -> Cluster:
        item = items[j]
        assert isinstance(item, Cluster)
        return item

    if rule == "largest":
        return max(candidates, key=lambda j: (cluster(j).size, -j))
    if rule == "density":  # fewest keys per byte freed
        return min(
            candidates,
            key=lambda j: (cluster(j).keys / (cluster(j).size - EXTERNAL), j),
        )
    if rule == "height":  # shallowest first, then largest
        return min(candidates, key=lambda j: (cluster(j).height, -cluster(j).size, j))
    raise SystemExit(f"unknown rule {rule}")


def pack_dense(
    corpus: Corpus, capacity: int, rule: str, encoder: Encoder | None = None
) -> DensePacking:
    packing = DensePacking(capacity, rule)
    budget = capacity - HEADER
    width = packing.slot_width()
    pages = packing.pages
    keep = encoder is not None

    def seal(cluster: Cluster) -> int:
        page = len(pages)
        pages.append(
            Page(
                HEADER + cluster.size,
                cluster.keys,
                cluster.height + 1,
                len(cluster.externals),
            )
        )
        for lo, hi in cluster.ranges:
            packing.leaf_runs.append((lo, hi, page))
        for child in cluster.externals:
            pages[child].parent = page
        if encoder is not None:
            encoder.emit(packing, page, cluster)
        return page

    def on_leaf(i: int, key: bytes, start: int) -> Cluster | Sealed:
        size = 4 + (len(key) - start) + corpus.body_length(i)
        packing.node_bytes += size
        packing.nodes += 1
        node = None
        if keep:
            tag = (
                TAG_DEFINITION if corpus.kinds[i] == KIND_DEFINITION else TAG_NAMESPACE
            )
            node = (tag, key[start:], corpus.body(i))
        cluster = Cluster(size, 1, 0, [[i, i + 1]], [], node)
        return Sealed(seal(cluster)) if size > budget else cluster

    def on_branch(
        key: bytes, start: int, depth: int, children: list[tuple[int, object]]
    ) -> Cluster | Sealed:
        own = 3 + (depth - start) + (1 + width) * len(children)
        packing.node_bytes += own
        packing.nodes += 1
        items: list[Cluster | Sealed] = []
        total = own
        for _, child in children:
            assert isinstance(child, (Cluster, Sealed))
            items.append(child)
            total += EXTERNAL if isinstance(child, Sealed) else child.size
        if total > budget:
            candidates = [
                j
                for j, item in enumerate(items)
                if isinstance(item, Cluster) and item.size > EXTERNAL
            ]
            while total > budget and candidates:
                j = choose(rule, items, candidates)
                candidates.remove(j)
                child = items[j]
                assert isinstance(child, Cluster)
                items[j] = Sealed(seal(child))
                total -= child.size - EXTERNAL
        keys = height = 0
        ranges: list[list[int]] = []
        externals: list[int] = []
        kids: list[object] = []
        for item in items:
            if isinstance(item, Sealed):
                page = pages[item.page]
                keys += page.keys
                height = max(height, page.height)
                externals.append(item.page)
                kids.append(item)
                continue
            keys += item.keys
            height = max(height, item.height)
            if ranges and item.ranges and ranges[-1][1] == item.ranges[0][0]:
                ranges[-1][1] = item.ranges[0][1]
                ranges.extend(item.ranges[1:])
            else:
                ranges.extend(item.ranges)
            externals.extend(item.externals)
            kids.append(item.node)
        node = (
            (TAG_BRANCH, key[start:depth], bytes(label for label, _ in children), kids)
            if keep
            else None
        )
        cluster = Cluster(total, keys, height, ranges, externals, node)
        # Still over budget with every child worth sealing sealed: the node is a page of its own,
        # above the capacity and below 64 KiB (at most 256 children of at most 68 bytes each).
        return Sealed(seal(cluster)) if total > budget else cluster

    root = build_trie(corpus.count, corpus.key, on_leaf, on_branch)
    if isinstance(root, Sealed):
        packing.root = root.page
    else:
        assert isinstance(root, Cluster)
        packing.root = seal(root)
    if encoder is not None:
        encoder.finish()
    return packing


# --- encoding: exact page bytes, one digest per page, 1 MiB segments ---------------------------


def segment_id(ordinal: int) -> bytes:
    return hashlib.sha256(b"model segment" + ordinal.to_bytes(8, "big")).digest()[:16]


class Encoder:
    """Writes each page when it is sealed: the header, nodes in preorder from offset 64, the
    content table, then the placement table. The digest is SHA-256 over bytes [0, 32) and
    [64, length - 20 * externals): everything but the digest field and the placement."""

    def __init__(
        self, keep: Path | None = None, memory: bool = False, compress: bool = True
    ) -> None:
        self.keep = keep
        self.memory: dict[bytes, bytes] | None = (
            {} if memory else None
        )  # segment id -> bytes
        self.compress = compress
        self.segment = bytearray()
        self.ordinal = 0
        self.raw = self.zstd1 = self.zstd3 = self.segments = self.hashed = 0
        self.digests: dict[
            bytes, int
        ] = {}  # digest -> page length, for diffs between builds
        self.placed: dict[int, tuple[int, int]] = {}  # page -> segment ordinal, offset

    def emit(self, packing: DensePacking, page_id: int, cluster: Cluster) -> None:
        width = packing.slot_width()
        flag = 1 << (8 * width - 1)
        body = bytearray(HEADER)
        externals: list[int] = []
        nodes = 0

        def write(node: object) -> int:
            nonlocal nodes
            nodes += 1
            at = len(body)
            assert isinstance(node, tuple)
            if node[0] != TAG_BRANCH:
                tag, suffix, payload = node
                body.extend((tag, len(suffix)))
                body.extend(len(payload).to_bytes(2, "big"))
                body.extend(suffix)
                body.extend(payload)
                return at
            _, prefix, labels, kids = node
            body.extend((TAG_BRANCH, len(prefix), len(labels) - 1))
            body.extend(prefix)
            body.extend(labels)
            slots = len(body)
            body.extend(bytes(width * len(labels)))
            for index, kid in enumerate(kids):
                if isinstance(kid, Sealed):
                    value = flag | len(externals)
                    externals.append(kid.page)
                else:
                    value = write(kid)
                    assert value < flag
                body[slots + width * index : slots + width * (index + 1)] = (
                    value.to_bytes(width, "big")
                )
            return at

        write(cluster.node)
        assert externals == cluster.externals
        pages = packing.pages
        for child in externals:
            page = pages[child]
            body.extend(page.digest)
            body.extend(page.keys.to_bytes(8, "big"))
            body.extend(page.length.to_bytes(4, "big"))
            body.extend((page.height, 0, 0, 0))
        covered = len(body)
        body.extend(bytes(PLACEMENT * len(externals)))
        page = pages[page_id]
        assert len(body) == page.length <= MAX_PAGE and nodes < 1 << 16, (
            len(body),
            page.length,
        )
        body[0:8] = b"DTPAGE03"
        body[8:12] = len(body).to_bytes(4, "big")
        body[12:14] = len(externals).to_bytes(2, "big")
        body[14:16] = nodes.to_bytes(2, "big")
        body[16:24] = page.keys.to_bytes(8, "big")
        body[24] = page.height
        h = hashlib.sha256(body[0:32])
        h.update(body[64:covered])
        self.hashed += covered - 32
        digest = h.digest()
        body[32:64] = digest
        page.digest = digest
        self.digests[digest] = page.length
        for index, child in enumerate(externals):
            at = covered + PLACEMENT * index
            ordinal, offset = self.placed[child]
            body[at : at + 16] = segment_id(ordinal)
            body[at + 16 : at + 20] = offset.to_bytes(4, "big")
        start = (len(self.segment) + 7) & ~7
        if start + len(body) > SEGMENT:
            self.close()
            start = 0
        self.segment.extend(bytes(start - len(self.segment)))
        self.placed[page_id] = (self.ordinal, start)
        self.segment.extend(body)

    def close(self) -> None:
        if not self.segment:
            return
        from compression import zstd

        data = bytes(self.segment)
        self.raw += len(data)
        if self.compress:
            self.zstd1 += len(zstd.compress(data, level=1))
            self.zstd3 += len(zstd.compress(data, level=3))
        if self.keep is not None:
            (self.keep / f"segment-{segment_id(self.ordinal).hex()}.pack").write_bytes(
                data
            )
        if self.memory is not None:
            self.memory[segment_id(self.ordinal)] = data
        self.segments += 1
        self.ordinal += 1
        self.segment = bytearray()

    def finish(self) -> None:
        self.close()


# --- reading: the walk a reader does over encoded pages ----------------------------------------


class PageReader:
    """Looks keys up in encoded segments as a reader would: a first touch hashes the page and
    compares the result with the reference and the header; a later touch of the same location
    compares the reference's digest with the header's (the hit path, D4)."""

    def __init__(self, segments: dict[bytes, bytes], width: int) -> None:
        self.segments = segments
        self.width = width
        self.flag = 1 << (8 * width - 1)
        self.verified: set[tuple[bytes, int]] = set()
        self.first_touches = self.hits = self.hashed = 0

    def page(self, segment: bytes, offset: int, length: int, digest: bytes) -> bytes:
        page = self.segments[segment][offset : offset + length]
        assert (
            len(page) == length and page[:8] == b"DTPAGE03" and be(page, 8, 4) == length
        )
        if (segment, offset) in self.verified:
            self.hits += 1
            assert page[32:64] == digest, "hit on a page with another digest"
            return page
        externals = be(page, 12, 2)
        covered = length - PLACEMENT * externals
        h = hashlib.sha256(page[0:32])
        h.update(page[64:covered])
        assert h.digest() == digest == page[32:64], "page digest mismatch"
        self.hashed += covered - 32
        self.first_touches += 1
        self.verified.add((segment, offset))
        return page

    def lookup(
        self, root: tuple[bytes, int, int, bytes], key: bytes
    ) -> tuple[bytes | None, set[tuple[bytes, int]]]:
        """The body stored under key, or None; and the pages the walk touched."""
        segment, offset, length, digest = root
        page = self.page(segment, offset, length, digest)
        touched = {(segment, offset)}
        at, cursor = HEADER, 0
        while True:
            tag = page[at]
            if tag != TAG_BRANCH:
                suffix_length, body_length = page[at + 1], be(page, at + 2, 2)
                if key[cursor:] != page[at + 4 : at + 4 + suffix_length]:
                    return None, touched
                return page[
                    at + 4 + suffix_length : at + 4 + suffix_length + body_length
                ], touched
            prefix_length, count = page[at + 1], page[at + 2] + 1
            labels_at = at + 3 + prefix_length
            if key[cursor : cursor + prefix_length] != page[at + 3 : labels_at]:
                return None, touched
            cursor += prefix_length
            if cursor >= len(key):
                return None, touched
            index = page.find(key[cursor : cursor + 1], labels_at, labels_at + count)
            if index < 0:
                return None, touched
            cursor += 1
            slot = be(
                page, labels_at + count + self.width * (index - labels_at), self.width
            )
            if not slot & self.flag:
                at = slot
                continue
            external, externals, end = slot & ~self.flag, be(page, 12, 2), len(page)
            content = end - EXTERNAL * externals + CONTENT * external
            placement = end - PLACEMENT * externals + PLACEMENT * external
            segment, offset = (
                page[placement : placement + 16],
                be(page, placement + 16, 4),
            )
            page = self.page(
                segment, offset, be(page, content + 40, 4), page[content : content + 32]
            )
            touched.add((segment, offset))
            at = HEADER


def verify_main(corpus: Corpus, capacity: int, rule: str, sample: int) -> None:
    """Encodes in memory, then looks keys up through the encoded bytes: every body must come back
    exact, a missing key must miss, and the pages each lookup touches must match the packing."""
    started = time.monotonic()
    encoder = Encoder(memory=True, compress=False)
    packing = pack_dense(corpus, capacity, rule, encoder)
    assert encoder.memory is not None
    root = packing.pages[packing.root]
    ordinal, offset = encoder.placed[packing.root]
    reference = (segment_id(ordinal), offset, root.length, root.digest)
    reader = PageReader(encoder.memory, packing.slot_width())
    depth = array("H", bytes(2 * len(packing.pages)))
    for p in range(len(packing.pages) - 1, -1, -1):
        up = packing.pages[p].parent
        depth[p] = 1 if up < 0 else depth[up] + 1
    page_of = packing.page_of_leaves(corpus.count)
    records = (
        range(corpus.count)
        if sample >= corpus.count
        else random.Random(0x564552).sample(range(corpus.count), sample)
    )
    both: dict[int, int] = {}
    for i in records:
        key = corpus.key(i)
        body, touched = reader.lookup(reference, key)
        assert body == corpus.body(i), key
        assert len(touched) == depth[page_of[i]], (key, len(touched), depth[page_of[i]])
        missing, _ = reader.lookup(reference, key[:-1] + b"\x01\0")
        assert missing is None, key
        if corpus.kinds[i] == KIND_DEFINITION:
            _, watermark = reader.lookup(reference, corpus.key(corpus.watermark[i]))
            pages = len(touched | watermark)
            both[pages] = both.get(pages, 0) + 1
    print(
        f"verify C {capacity:,} {rule}: {len(records):,} keys found exactly through the encoded pages;"
    )
    print(
        f"  first touches {reader.first_touches:,} ({reader.hashed:,} B hashed), hits {reader.hits:,};"
    )
    print(
        f"  pages per definition lookup median {histogram_quantile(both, 0.5)}, p99 {histogram_quantile(both, 0.99)};"
    )
    print(f"  root digest {root.digest.hex()[:16]}; {time.monotonic() - started:.0f} s")


# --- reports -----------------------------------------------------------------------------------


@dataclass(slots=True)
class DensePaths:
    walk: dict[int, int]
    both: dict[int, int]
    touch: list[int]  # bytes one lookup hashes when its pages are all cold, sorted
    bottom: list[int]  # bytes the first touch of the definition's page hashes, sorted


def dense_paths(
    corpus: Corpus, packing: DensePacking, page_of: array[int]
) -> DensePaths:
    pages = packing.pages
    n = len(pages)
    depth = array("H", bytes(2 * n))
    path = array("Q", bytes(8 * n))
    hashed = [p.length - 32 - PLACEMENT * p.externals for p in pages]
    for p in range(n - 1, -1, -1):  # parents are sealed after their children
        up = pages[p].parent
        depth[p], path[p] = (
            (1, hashed[p]) if up < 0 else (depth[up] + 1, path[up] + hashed[p])
        )
    shared: dict[int, tuple[array[int], array[int]]] = {}
    for w in sorted(set(corpus.watermark)):
        on_path: set[int] = set()
        q = page_of[w]
        while q >= 0:
            on_path.add(q)
            q = pages[q].parent
        top_depth, top_bytes = array("H", bytes(2 * n)), array("Q", bytes(8 * n))
        for p in range(n - 1, -1, -1):
            if p in on_path:
                top_depth[p], top_bytes[p] = depth[p], path[p]
            else:
                up = pages[p].parent
                top_depth[p], top_bytes[p] = top_depth[up], top_bytes[up]
        shared[w] = (top_depth, top_bytes)
    walk: dict[int, int] = {}
    both: dict[int, int] = {}
    touch = array("Q")
    bottom = array("Q")
    for i in range(corpus.count):
        if corpus.kinds[i] != KIND_DEFINITION:
            continue
        p, w = page_of[i], corpus.watermark[i]
        wp = page_of[w]
        top_depth, top_bytes = shared[w]
        walk[depth[p]] = walk.get(depth[p], 0) + 1
        count = depth[p] + depth[wp] - top_depth[p]
        both[count] = both.get(count, 0) + 1
        touch.append(path[p] + path[wp] - top_bytes[p])
        bottom.append(hashed[p])
    return DensePaths(walk, both, sorted(touch), sorted(bottom))


def pack_main(corpus: Corpus, capacities: list[int], rules: list[str]) -> None:
    for capacity in capacities:
        for rule in rules:
            started = time.monotonic()
            packing = pack_dense(corpus, capacity, rule)
            pages = packing.pages
            total = sum(p.length for p in pages)
            externals = sum(p.externals for p in pages)
            over = [p.length for p in pages if p.length > capacity]
            segments, segment_bytes = segments_for([p.length for p in pages])
            page_hashed = total - 32 * len(pages) - PLACEMENT * externals
            built = page_hashed + segment_bytes
            paths = dense_paths(corpus, packing, packing.page_of_leaves(corpus.count))
            w50, w99 = (
                histogram_quantile(paths.walk, 0.5),
                histogram_quantile(paths.walk, 0.99),
            )
            b50, b99 = (
                histogram_quantile(paths.both, 0.5),
                histogram_quantile(paths.both, 0.99),
            )
            t50, t99 = quantile(paths.touch, 0.5), quantile(paths.touch, 0.99)
            lengths = sorted(p.length for p in pages)
            digests = len(pages) + segments
            print(
                f"C {capacity:,} {rule}: {len(pages):,} pages, {total:,} B ({total / corpus.count:.1f} B per record)"
            )
            print(
                f"  nodes {packing.nodes:,} ({packing.node_bytes:,} B), headers {HEADER * len(pages):,} B,"
            )
            print(
                f"  externals {externals:,} ({EXTERNAL * externals:,} B); page length median {quantile(lengths, 0.5):,},"
            )
            print(
                f"  p10 {quantile(lengths, 0.1):,}, mean {total / len(pages):,.0f}; over capacity {len(over):,}"
            )
            print(
                f"  segments {segments:,} ({segment_bytes:,} B); SHA-256 at build {page_hashed:,} + {segment_bytes:,}"
            )
            print(
                f"  = {built:,} B in {digests:,} digests (estimate {sha_us(built, digests) / 1e6:.2f} s)"
            )
            print(
                f"  pages per definition walk: median {w50}, p99 {w99}, max {max(paths.walk)};"
            )
            print(
                f"  with the watermark walk: median {b50}, p99 {b99}, max {max(paths.both)}"
            )
            print(
                f"  cold lookup hashes: median {t50:,} B, p99 {t99:,} B (estimate {sha_us(t50, b50):.0f} us"
            )
            f50, f99 = quantile(paths.bottom, 0.5), quantile(paths.bottom, 0.99)
            print(
                f"  and {sha_us(t99, b99):.0f} us); definition page first touch: median {f50:,} B,"
            )
            print(
                f"  p99 {f99:,} B (estimate {sha_us(f50):.1f} us and {sha_us(f99):.1f} us); height"
            )
            print(f"  {pages[packing.root].height}; {time.monotonic() - started:.0f} s")
            sys.stdout.flush()


def bytes_main(corpus: Corpus, capacity: int, rule: str, keep: Path | None) -> None:
    if keep is not None:
        keep.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    encoder = Encoder(keep)
    packing = pack_dense(corpus, capacity, rule, encoder)
    total = sum(p.length for p in packing.pages)
    raw = encoder.raw
    print(
        f"C {capacity:,} {rule}: {len(packing.pages):,} pages, {total:,} page bytes, {encoder.segments:,} segments,"
    )
    print(f"  {raw:,} segment bytes ({raw / corpus.count:.1f} B per record)")
    print(
        f"  zstd-1 {encoder.zstd1:,} B ({100 * encoder.zstd1 / raw:.1f}%), zstd-3 {encoder.zstd3:,} B"
    )
    print(
        f"  ({100 * encoder.zstd3 / raw:.1f}%); page digests over {encoder.hashed:,} B"
    )
    print(
        f"  root digest {packing.pages[packing.root].digest.hex()}; {time.monotonic() - started:.0f} s"
    )


# --- deltas ------------------------------------------------------------------------------------


def path_union(
    parent_of: Callable[[int], int], starts: Sequence[int], cost: Callable[[int], int]
) -> tuple[int, int]:
    """Distinct units on the root paths of starts, and the sum of their cost."""
    seen: set[int] = set()
    total = 0
    for unit in starts:
        while unit >= 0 and unit not in seen:
            seen.add(unit)
            total += cost(unit)
            up = parent_of(unit)
            unit = -1 if up == unit else up
    return len(seen), total


def delta_main(corpus: Corpus, capacities: list[int], rule: str) -> None:
    started = time.monotonic()
    today = build_today(corpus)
    today_total = sum(today.size)
    definitions = [i for i in range(corpus.count) if corpus.kinds[i] == KIND_DEFINITION]
    print(
        f"today: {today_total:,} page bytes; built in {time.monotonic() - started:.0f} s",
        flush=True,
    )
    dense: list[tuple[int, list[int], list[int], list[int], array[int]]] = []
    for capacity in capacities:
        packing = pack_dense(corpus, capacity, rule)
        parents = [p.parent for p in packing.pages]
        lengths = [p.length for p in packing.pages]
        hashed = [p.length - 32 - PLACEMENT * p.externals for p in packing.pages]
        dense.append(
            (capacity, parents, lengths, hashed, packing.page_of_leaves(corpus.count))
        )
    rng = random.Random(0x44454C5441)
    shapes = [
        (1, False, 2000),
        (100, False, 200),
        (100, True, 50),
        (10_000, False, 10),
        (10_000, True, 10),
    ]
    for size, clustered, trials in shapes:
        rows: dict[str, list[tuple[int, int, int]]] = {}
        for _ in range(trials):
            if clustered:
                first = rng.randrange(len(definitions) - size)
                changed = definitions[first : first + size]
            else:
                changed = rng.sample(definitions, size)
            leaves = set(changed) | {corpus.watermark[i] for i in changed}
            starts = [today.leaf_node[i] for i in leaves]
            count, written = path_union(
                today.parent.__getitem__, starts, today.size.__getitem__
            )
            _, verified = path_union(
                today.parent.__getitem__, starts, today.verify.__getitem__
            )
            rows.setdefault("today", []).append((count, written, verified))
            for capacity, parents, lengths, hashed, page_of in dense:
                starts = [page_of[i] for i in leaves]
                count, written = path_union(
                    parents.__getitem__, starts, lengths.__getitem__
                )
                _, digest_bytes = path_union(
                    parents.__getitem__, starts, hashed.__getitem__
                )
                rows.setdefault(str(capacity), []).append(
                    (count, written, digest_bytes)
                )
        shape = "clustered" if clustered else "uniform"
        print(
            f"{size:,} keys, {shape}, {trials} trials: mean pages, page bytes written, page bytes hashed"
        )
        for name, total in [("today", today_total)] + [
            (str(c), sum(lengths)) for c, _, lengths, _, _ in dense
        ]:
            row = rows[name]
            written = sorted(r[1] for r in row)
            label = "today" if name == "today" else f"{int(name) // 1024} KiB"
            print(
                f"  {label:>6} {mean([r[0] for r in row]):>9,.0f} {mean(written):>13,.0f} {mean([r[2] for r in row]):>13,.0f}",
                end="",
            )
            print(
                f"  reuse {100 * (1 - mean(written) / total):.3f}%, written p50 {quantile(written, 0.5):,} p99 {quantile(written, 0.99):,}"
            )
        sys.stdout.flush()


def mutate(
    corpus: Corpus, rng: random.Random, updates: int, inserts: int, deletes: int
) -> tuple[CorpusWriter, set[bytes]]:
    """Same-size updates with a new revision, fresh keys inserted beside existing ones, keys
    removed, and every touched namespace's watermark advanced. Returns the new corpus's writer and
    the keys whose records changed or appeared, with a deleted key's neighbours, whose paths hold
    the pages its removal changes."""
    definitions = [i for i in range(corpus.count) if corpus.kinds[i] == KIND_DEFINITION]
    chosen = rng.sample(definitions, updates + deletes + inserts)
    updated, deleted = set(chosen[:updates]), set(chosen[updates : updates + deletes])
    beside = chosen[updates + deletes :]
    bump = 1 << 32
    changed: set[bytes] = set()
    touched: set[int] = set()
    records: dict[bytes, tuple[int, bytes]] = {}
    for i in range(corpus.count):
        key, body = corpus.key(i), corpus.body(i)
        if i in deleted:
            touched.add(corpus.watermark[i])
            changed.update(
                corpus.key(j) for j in (i - 1, i + 1) if 0 <= j < corpus.count
            )
            continue
        if i in updated:
            body = (be(body, 0, 8) + bump).to_bytes(8, "big") + body[8:]
            changed.add(key)
            touched.add(corpus.watermark[i])
        records[key] = (corpus.kinds[i], body)
    for i in beside:
        key = corpus.key(i)[:-1] + b"x\0"
        records[key] = (KIND_DEFINITION, corpus.body(i))
        changed.add(key)
        touched.add(corpus.watermark[i])
    for w in touched:
        key = corpus.key(w)
        kind, body = records[key]
        records[key] = (kind, (be(body, 0, 8) + bump).to_bytes(8, "big"))
        changed.add(key)
    writer = CorpusWriter()
    for key in sorted(records):
        kind, body = records[key]
        writer.add(key, kind, body)
    return writer, changed


def churn_main(
    corpus: Corpus,
    capacity: int,
    rule: str,
    updates: int,
    inserts: int,
    deletes: int,
    out: Path | None = None,
) -> None:
    started = time.monotonic()
    writer, changed = mutate(
        corpus, random.Random(0x434855524E), updates, inserts, deletes
    )
    after = writer.corpus()
    if out is not None:  # the changed corpus, a delta's oracle (T1, T4)
        writer.write(out)
    old_today, new_today = (
        build_today(corpus, digests=True),
        build_today(after, digests=True),
    )
    old_nodes = set(old_today.logical)
    fresh = [
        n for n in range(len(new_today.size)) if new_today.logical[n] not in old_nodes
    ]
    today_bytes = sum(new_today.size[n] for n in fresh)
    old = Encoder()
    pack_dense(corpus, capacity, rule, old)
    new = Encoder()
    packing = pack_dense(after, capacity, rule, new)
    pages = packing.pages
    fresh_pages = {p for p in range(len(pages)) if pages[p].digest not in old.digests}
    page_of = packing.page_of_leaves(after.count)
    on_path: set[int] = set()
    for i in range(after.count):
        if after.key(i) in changed:
            q = page_of[i]
            while q >= 0 and q not in on_path:
                on_path.add(q)
                q = pages[q].parent
    # Pages written off every changed path are sealing decisions that moved (D5): clean
    # clusters a dirty node now seals, so their parents lie on a changed path.
    off_path = fresh_pages - on_path
    assert all(pages[p].parent in on_path for p in off_path)
    total = sum(p.length for p in pages)
    dense_bytes = sum(pages[p].length for p in fresh_pages)
    freed = sum(n for d, n in old.digests.items() if d not in new.digests)
    print(
        f"churn at C {capacity:,} ({rule}): {updates:,} updates, {inserts:,} inserts, {deletes:,} deletes"
    )
    print(f"  today writes {len(fresh):,} pages, {today_bytes:,} B")
    print(
        f"  dense writes {len(fresh_pages):,} pages, {dense_bytes:,} B of {total:,} ({100 * dense_bytes / total:.3f}%);"
    )
    print(
        f"  {len(fresh_pages & on_path):,} on a changed path, {len(off_path):,} off it"
    )
    print(
        f"  ({sum(pages[p].length for p in off_path):,} B); pages on changed paths {len(on_path):,}, unchanged among"
    )
    print(
        f"  them {len(on_path - fresh_pages):,}; old pages left unreachable {freed:,} B; {time.monotonic() - started:.0f} s"
    )


# --- the synthetic corpus and the self-check ----------------------------------------------------


def synthetic_corpus() -> CorpusWriter:
    """Four namespaces of 5,000 definitions with 24- and 73-byte bodies, and one namespace whose
    200 small leaves share one branch, a page above capacity at 4 KiB (D5)."""
    rng = random.Random(7)
    records: dict[bytes, tuple[int, bytes]] = {}
    revision = 1
    for ns in (b"load0", b"load1", b"load2", b"load3"):
        for n in range(5000):
            revision += 1
            size = 73 if n % 2 else 24
            body = revision.to_bytes(8, "big") + bytes(
                rng.randrange(256) for _ in range(size - 8)
            )
            records[b"K" + ns + b"\0key" + str(n).encode() + b"\0"] = (
                KIND_DEFINITION,
                body,
            )
        records[b"W" + ns + b"\0"] = (KIND_NAMESPACE, revision.to_bytes(8, "big"))
    for c in range(33, 233):
        records[b"Kwide\0a" + bytes([c]) + b"\0"] = (KIND_DEFINITION, bytes(24))
    records[b"Wwide\0"] = (KIND_NAMESPACE, bytes(8))
    writer = CorpusWriter()
    for key in sorted(records):
        writer.add(key, *records[key])
    return writer


def check_main(directory: Path) -> None:
    """Runs every mode on the synthetic corpus and checks the packings, the encoded round trip,
    identical builds, a delta's changed corpus and the corpus file format."""
    directory.mkdir(parents=True, exist_ok=True)
    writer = synthetic_corpus()
    corpus = writer.corpus()
    print(f"synthetic corpus: {corpus.count:,} records")
    trie = build_today(corpus)
    assert len(trie.size) == corpus.count + trie.branches
    assert trie.child_count == len(trie.size) - 1
    today_main(corpus)
    roots: set[bytes] = set()
    for capacity in (512, 4096, 65536):
        for rule in ("largest", "density", "height"):
            verify_main(corpus, capacity, rule, corpus.count)
            packing = pack_dense(corpus, capacity, rule)
            covered = sorted((lo, hi) for lo, hi, _ in packing.leaf_runs)
            assert covered[0][0] == 0 and covered[-1][1] == corpus.count
            assert all(a[1] == b[0] for a, b in itertools.pairwise(covered))
            assert packing.pages[packing.root].keys == corpus.count
            assert sum(1 for p in packing.pages if p.parent < 0) == 1
            if capacity == 4096:
                assert any(p.length > capacity for p in packing.pages)
        first, second = (
            Encoder(memory=True, compress=False),
            Encoder(memory=True, compress=False),
        )
        a = pack_dense(corpus, capacity, "height", first)
        b = pack_dense(corpus, capacity, "height", second)
        assert a.pages[a.root].digest == b.pages[b.root].digest
        assert first.memory == second.memory, "two builds must give identical segments"
        roots.add(a.pages[a.root].digest)
    assert len(roots) == 3, "capacities pack differently, so their roots differ"
    alone = reshape(corpus, b"load1", "hex")
    assert alone.count == 5001 and alone.kinds.count(KIND_NAMESPACE) == 1
    verify_main(alone, 4096, "height", alone.count)
    pack_main(corpus, [4096], ["largest", "density", "height"])
    bytes_main(corpus, 4096, "height", None)
    delta_main(corpus, [4096, 65536], "height")
    churn_main(corpus, 4096, "height", 0, 0, 0)
    after_path = directory / "synthetic-after.bin"
    churn_main(corpus, 4096, "height", 100, 10, 10, after_path)
    after = read_corpus(after_path)
    assert after.count == corpus.count and after.keys != corpus.keys
    verify_main(after, 4096, "height", after.count)
    corpus_path = directory / "synthetic.bin"
    writer.write(corpus_path)
    again = read_corpus(corpus_path)
    assert again.keys == corpus.keys and again.bodies == corpus.bodies
    assert list(again.watermark) == list(corpus.watermark)
    print("check: ok")


# --- main --------------------------------------------------------------------------------------


def main(argv: list[str]) -> None:
    if len(argv) < 3:
        raise SystemExit(__doc__)
    mode = argv[1]
    if mode == "extract":
        extract_main(Path(argv[2]), Path(argv[3]))
        return
    if mode == "synthetic":
        synthetic_corpus().write(Path(argv[2]))
        return
    if mode == "check":
        check_main(Path(argv[2]))
        return
    started = time.monotonic()
    options: dict[str, str] = {}
    for argument in argv[3:]:
        if argument.startswith("--"):
            name, _, value = argument[2:].partition("=")
            options[name] = value
    rest = [a for a in argv[3:] if not a.startswith("--")]
    rule_list = options.get("rule", "largest").split(",")
    namespace = options["namespace"].encode() if "namespace" in options else None
    corpus = reshape(
        read_corpus(Path(argv[2])), namespace, options.get("shape", "store")
    )
    print(
        f"corpus: {corpus.count:,} records, {len(corpus.keys):,} key bytes, {len(corpus.bodies):,} body bytes",
        end="",
    )
    print(
        f" ({options or 'as stored'}); loaded in {time.monotonic() - started:.0f} s",
        flush=True,
    )
    if mode == "today":
        today_main(corpus)
    elif mode == "pack":
        pack_main(
            corpus,
            [int(x) for x in rest] or [4096, 8192, 16384, 32768, 65536],
            rule_list,
        )
    elif mode == "bytes":
        bytes_main(
            corpus, int(rest[0]), rule_list[0], Path(rest[1]) if len(rest) > 1 else None
        )
    elif mode == "verify":
        verify_main(
            corpus,
            int(rest[0]),
            rule_list[0],
            int(rest[1]) if len(rest) > 1 else 100_000,
        )
    elif mode == "delta":
        delta_main(corpus, [int(x) for x in rest] or [4096, 16384, 65536], rule_list[0])
    elif mode == "churn":
        counts = [int(x) for x in rest[1:4]] if len(rest) >= 4 else [9_800, 100, 100]
        out = Path(options["out"]) if "out" in options else None
        churn_main(
            corpus, int(rest[0]), rule_list[0], counts[0], counts[1], counts[2], out
        )
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
