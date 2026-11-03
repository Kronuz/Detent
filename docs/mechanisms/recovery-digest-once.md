> Status: CURRENT (2026-11-03) how managed recovery compares each segment checksum and page digest once, read from `src/detent_local/` at the head of `phase/hash-once`.

# Recovery compares each digest once

A cold paged recovery (`RecoveryEngine::recover`, `generation.cc`) runs two graph passes over the same
pack bytes. Each pass used to compute and compare every digest; now the first pass compares and the
second skips what the first already proved.

## The digests

Three different digests protect a pack, and each has its own comparison:

| Digest | Over | Compared with | Compared in |
| --- | --- | --- | --- |
| Segment checksum | the whole pack file | `ownership.checksum` in the `.owner` record | `DependencyDiscovery` (Hash phase) |
| Page physical digest | the page's bytes | `reference.physical` from the parent page or root | `PageValidator`, Physical phase |
| Page logical digest | the page's header frame, prefix, child labels with child logical digests, and body | `reference.logical` | `PageValidator`, Seal phase |

The segment checksum covers padding between pages that no page digest covers; the page digests
chain to the root image, which `selected` authenticates, and the segment checksum does not. All
three comparisons stay, so a pack is hashed three times, not six.

## The two passes

1. `DependencyDiscovery` walks the tree from the root page. Each segment it needs is supplied by the
   recovery loop, hashed against its owner record, and inserted into the sorted registry. Its
   `PagedDependencyWalk` runs a `PageValidator` per page, which compares both page digests.
2. `GraphValidator` walks the same tree over the finished registry for the semantic checks (key
   order, counts, definitions, extent overlap). It used to hash every segment again (`Segments`
   phase) and run a second `PageValidator` per page.

## The carried result

When discovery completes without a prior registry (no frontier skips any page), it calls
`SegmentRegistry::certify_digests(root)` after `seal()` (`paged_dependency_discovery.cc`). The
registry then holds `compared_root()`: every segment in it passed its checksum, and every page
reachable from that root passed both page digests.

`GraphValidator` takes the registry as an optional last constructor argument, `compared`. The
constructor (`digests_compared`, `paged_graph_validator.cc`) requires that the registry's
certified root encodes equal to the validator's root and that the registry's inputs are the very
span being validated; any mismatch throws `std::invalid_argument`. When it passes:

- the `Segments` phase is skipped (`hashed_segment_bytes()` stays 0);
- each `PageValidator` is built with `compare_digests = false` and does not hash or compare, while
  its header decode, `header.logical == reference.logical` check, child label ordering and
  everything else in `GraphValidator` (extent overlap marking, key and count checks) still run.

Without `compared`, `GraphValidator` compares everything, as before. The warm path
(`WarmGraphValidator`, a prior registry) is unchanged.

The skip is sound because the second pass walks the same root over the same registry inputs, whose
pages are read-only mappings held under the generation lease, so the bytes it reads are the bytes
the first pass compared.

## Where a corrupt pack is rejected

A flipped pack byte is rejected by discovery, in the same phase and with the same error as before:

- Anywhere in the pack, owner record intact: `invalid paged dependency graph` (segment checksum).
- Inside a page, owner record rewritten to match: `invalid immutable page` (page physical digest) or
  a structural error from the page decode.
- In padding, owner record rewritten to match: accepted, as before; no digest covers it.

The checks `GraphValidator` no longer repeats cannot fail once discovery has passed on the same
bytes. `tests/detent_local/test_paged_hash_once.cc` flips every byte of a stored pack both ways and
records the rejection; `test_paged_snapshot_builder.cc` validates a certified registry and a
mismatched one.
