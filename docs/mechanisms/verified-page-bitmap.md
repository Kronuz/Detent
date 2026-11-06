# Verified-page bitmap

A lazily mapped generation resolves page references through one segment table and one verification map per segment. Warm access compares the reached page header with the incoming reference before it uses a verification mark (`src/detent_local/lazy_page_access.cc:190`).

```text
reference -> segment table -> bounded page span -> header/reference comparison
                                                      |
                                  exact start marked? +-- yes -> bounded node walk
                                                      +-- no  -> verify page, mark
```

## Segment table

`slots` is an admitted array of atomic segment pointers, sized to a power of two at least twice `root.segments`. A lookup probes by segment identity. A missing segment is opened with owner, mode, extent and inode checks, mapped read-only, and published with release compare-and-swap. A competing mapper retires its unused mapping and uses the winner. Published pointers remain stable for the generation's lifetime.

Every mapping counts against the segment and mapped-byte limits. A refused admission probes again before it fails, so an entry another thread publishes meanwhile can satisfy the lookup. Loads use acquire ordering. A warm lookup needs no lock, allocation or syscall.

## Verified starts

Each 80-byte granule carries a four-bit code for an exact aligned page start (`src/detent_local/verified_pages.hh:9`). Dense pages occupy at least 79 bytes and start at 8-byte offsets, so disjoint pages have starts at least 80 bytes apart. A mark for one start cannot validate an interior offset in the same granule.

Before consulting that mark, `resolve` bounds the reference within its segment and decodes DTPAGE03 framing. Length, key count, height and digest must equal the reached header. A clear mark triggers one page-digest comparison, complete preorder structure validation and leaf semantic checks. Successful verification publishes the exact start with release ordering; a hit reads it with acquire ordering. Competing first touches may both hash the page.

A marked page still bounds each node visit, inline slot, prefix, suffix, body and external reference. Each branch consumes key bytes, and the walk has a bounded visit count. Redirected placement into another verified page fails the header comparison. Framing-valid value changes that preserve the compared header may remain undetected until scrub; the mark does not promise integrity against arbitrary writes after verification.

DTPAGE02 framing refuses first touch and marks the generation failed. Startup's automatic old-format refetch requires authenticated version-2 generation ownership, as described in [dense pages](dense-pages.md#refusal-and-reclamation).

## Lifetime and diagnostics

The retirement domain releases mappings and their admitted verification maps after readers finish. A page's mark lasts for that exact mapping. `mapped_segments()` counts published segments; `page_verifications()` counts completed first touches. `LazyPageTesting::install` exposes the miss, publish and mark windows for concurrency tests and is installed before lookups.
