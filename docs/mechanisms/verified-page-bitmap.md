> Status: DRAFT (2026-11-03) Branch `phase/reader-s3`; reader design D3 inside `LazyPageAccess` (`docs/design/reader.md`, step S3). Workstream ledger: `docs/status/reader-s3.md`.

# Verified-page bitmap

How a lazily mapped generation resolves a page reference without a lock: one segment table per generation and one verified bit per page. Code: `src/detent_local/lazy_page_access.cc`, `lazy_page_access.hh`.

## Shape

```
resolve(ref) -> failed? -> slots[hash(ref.segment)] --acquire--> Segment
                                 | empty: map, CAS publish        | mapping, bytes, verified[]
                                 v                                v
                         bit(ref.offset / 80) --acquire--> set: return bytes
                                                          clear: verify, fetch_or(release)
```

A lookup checks the generation's sticky `failed` flag, finds the segment, bounds the page by the segment's extent, tests the page's bit and returns the bytes. Nothing on a warm lookup locks, allocates or makes a syscall.

## Segment table

- `slots` is an open-addressed array of atomic `Segment *`, sized at construction to the power of two at or above `2 × root.segments` (at least 2) and charged to the heap ledger. Slots go from null to a segment and never back while lookups run, so linear probing needs no tombstones: a probe ends at the matching identity or at the first null slot.
- A miss maps the segment with today's checks (open with `O_NOFOLLOW`, extent, owner and mode, mode-only ACL, the mapped-bytes cap, the name's device and inode after mapping), builds a `Segment` (identity, mapping handle, byte span, zeroed bitmap) and publishes it with a release compare-and-swap at the first null slot. A thread that loses to the same identity destroys its copy, whose mapping goes to the retirement domain, and uses the winner's. A thread that loses to another identity probes on.
- Lookups load slots with acquire, so a reader sees the segment's fields as constructed. With relaxed orderings these field reads race with construction.
- The winner counts the segment and its bytes after publishing and refuses past `root.segments` or the mapped-bytes cap. A miss at the cap throws `ReadSourceFailure`, and the catch-all marks the generation failed, as before this change. A pre-check before the mapping refuses the serial case before any mapping is made.
- A full table (only reachable past the segment count) refuses the same way.

## Verified bits

- Each segment carries `size / 80 + 1` bits rounded up to 64-bit words: one per 80-byte granule. References need `offset % 8 == 0` and `length >= 80` (`paged_layout.cc:25-30`) and validated pages are disjoint (the validators' cell marks, `paged_graph_validator.cc:254-261`), so distinct pages own distinct granules. `resolve` also refuses a length below 80, which keeps the granule inside the bitmap.
- A set bit returns the bytes after the extent check, without hashing and without comparing the reference's digests with the ones that verified the page; the re-check of D3 ruled that sound.
- A clear bit verifies the page as before: `verify_page`, header decode, definition validation or the namespace revision check. Success counts one verification and sets the bit with a release `fetch_or`; failure marks the generation failed and sets nothing. Two threads that verify one page both set its bit.
- The bit's release pairs with the test's acquire, so a hit happens after a completed verification. The bytes are immutable while mapped, so the ordering protects no data today; relaxed would be race-free too.
- A page verifies once per mapping. There is no eviction: a bit clears only when its segment object is freed.

## Lifetime

`retire_step` releases a budget of segments per step (each `exchange`s its slot to null and destroys the segment), then frees the state. The destructor releases whatever is left. Each segment and its bitmap come from the heap ledger; the mappings from the mapping ledger.

## Diagnostics and test hooks

- `mapped_segments()` is the number of published segments; `page_verifications()` is the number of completed verifications.
- `LazyPageTesting::install` sets a hook that runs before a segment's publish (`Publish`) and before a verified bit is set (`Mark`). Tests use it to hold two threads at one window. It is installed before lookups and costs one relaxed load on a miss.
