# reader-s6: journal

State: working
Quest: .campaign/detent/quests/reader-s6.md (the lead's; never edit it)

## Doing

Merging main (`Detent:1180d74`, 2,484 commits since `Detent:f4f5263`) into the S6 branch and
porting the core to main's shapes: one knob type (D1's single `detent_get` with hash and switch),
no `StorageIdentity`/`storage.meta`, the `selected` table read by `detail::read_selected`, zero-byte
leases, `SegmentId`, `state_commitment`, `minimum_page_bytes`.

## Next

1. Build the reader targets; run reader tests and lazy-page tests locally (light hold).
2. Commit the merge; push; dispatch CI.
3. Once msg-79 is answered: SR1/FIX-2 in `generation.cc` `read_selected` (O_NONBLOCK; nlink 0 → nullopt).
4. Jobs: native, asan, tsan, t3-asan, t3-tsan; mutants (A8); SR1/SR2 regressions (A10, A11); fork
   freeze cases and soak (A12, A13); A14 stress; A16 first-read gate.

## Blocked on

- Lead (msg-79): `src/detent_local/generation.cc` outside the file set. Main's `read_selected` opens
  `selected` and the image without `O_NONBLOCK` (SR1, A10) and throws on a `selected` renamed over
  between open and fstat (FIX-2, A8). Recommended: add `generation.cc` to the file set.
- Lead (DES-7, msg-2): a full merge of `LazyPageAccess::resolve` with `checked_page_bytes` needs
  `src/detent_local/paged_navigation.hh` (outside the file set) to take view bounds and a
  post-verify node check. Done inside the set: resolve's hit path is `checked_page_bytes`'s field
  compare (no decode on a hit), the full decode and compare precede the bitmap set on a miss.

## Evidence

| Criterion | Evidence |
|---|---|
| A1-A16 | none yet at the merged head |

## Messages

- msg-1: open; T1 numbers are the lead's to measure on the merged head (exclusive `reader_bench`).
- msg-2: applied in the merge: `minimum_page_bytes` for the pack extent, header compare before any
  bitmap answer, `layout_version` gone with main's lease, `state_commitment` replaces `root.logical`.
  DES-7 partial (see Blocked on).

## Result

Pending.

## Rulings I made

- Main removed `StorageIdentity` and `storage.meta`; the core's "store identity change" refusal goes
  with them. A swapped store still fails the A4 regression check within an incarnation and the
  `change` identity check on refresh.
- C ABI follows main's reader.md D1 (one-knob-type): `detent_get(handle, name, hash, switch,
  fallback, out)`; `detent_eval`, `detent_read`, `detent_value`, `DETENT_WRONG_TYPE` removed;
  statuses renumbered to D1's table. C++ `Reader::get`, `get_as<T>` (out-of-range or a non-0/1
  bool returns the default), `lookup(name, fallback, ...)` per D7.
- A verified hit compares the reference's length, keys, height and digest with the page header's
  raw fields (as `checked_page_bytes`), without a full decode.
