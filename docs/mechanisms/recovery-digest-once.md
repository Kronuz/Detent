# Recovery compares each digest once

A full paged recovery runs discovery and semantic graph validation over the same pack bytes. Discovery compares every pack checksum and reachable page digest. The graph pass reuses that evidence while checking canonical packing and semantics (`src/detent_local/paged_dependency_discovery.cc`, `paged_graph_validator.cc`).

| Digest | Input | Expected value |
| --- | --- | --- |
| Pack checksum | Every file byte, including placement and padding | The owned segment record's checksum |
| Page digest | Header outside its digest field, nodes and external-child content | The parent reference's digest |
| Root checksum | Every byte of DTROOT03 | The selected descriptor's checksum |

Each page has one placement-free digest. Its reference also binds length, key count and height to the reached header. Page relocation preserves that digest. Pack checksums cover the placement bytes outside page commitments.

## Discovery and the carried proof

`DependencyDiscovery` obtains each owned pack, compares its checksum and inserts it into the admitted sorted registry. `PagedDependencyWalk` validates every reachable page. When a complete discovery skips no frontier, `SegmentRegistry::certify_digests(root)` binds that evidence to the exact root and registry inputs.

`GraphValidator` accepts that registry as optional evidence. It requires the certified root to encode identically to its target and the input span to be the registry's own span. It then skips pack hashing and creates page validators with `compare_digests = false`. Header/reference equality and structural validation still run (`src/detent_local/paged_page_validator.cc:45`). Canonical packing, key order, counts, leaf semantics and disjoint extents remain graph checks.

Only discovery can certify the registry. Recording final reachability clears the certificate, so it cannot authorize a later recovery. A graph validator without this evidence compares all digests. Warm validation uses its checked prior graph and validates changed paths separately.

## Immutability and refusal

The proof assumes that pack bytes remain unchanged between discovery and graph validation. A generation lease protects file lifetime; owner, mode, size and inode checks do not prevent a trusted writer from editing bytes. A correctly framed value change after verification can remain undetected until scrub. Reader visits retain bounds checks even on verified pages.

An intact owner record makes a flipped pack byte fail the pack checksum. If the owner checksum is rewritten, page content changes fail their page commitment or structural checks. Padding with a matching rewritten pack checksum is outside the state commitment. Page, pack and metadata hash counters record their input separately.

Authenticated ownership in a layout that is not current produces managed startup's [full-refetch path](dense-pages.md#refusal-and-reclamation) without decoding the generation. Bytes in another framing under current ownership are damage and cannot authorize selection removal. Trusted restart reads its authenticated root and derived records, then maps packs without a graph scan. Page first touch and scrub supply its subsequent integrity checks.
