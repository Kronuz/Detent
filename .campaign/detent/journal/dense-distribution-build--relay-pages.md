# dense-distribution-build--relay-pages: journal

State: working
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

The relay's half of DD3: it builds from the records it mirrors and answers W5 sessions that ask
for pages with pages frames (full copies, and D6 deltas over its page window).

## Doing

Building and running tests/detent_relay/test_relay_pages.cc (local build in .scratch/build).

## Next

1. Green test_relay_pages and the relay suite as jobs; push.

## Open-field contract (for agent-pages and the lead)

W5 open, version 2, gains two optional fields (open only; next and close unchanged):

- `"pages": true`: the session takes pages frames. Absent or false: records only (a downstream
  relay never sends it; its mirror refuses a pages frame).
- `"records": [names]`: strictly increasing; these namespaces get records even with `"pages":true`
  (D9: after a refused transfer, one other source, then records). A name not followed is ignored.

What a pages session gets, per namespace:

- Its answer would be a records full copy (no store, other incarnation, below the cut): a pages
  frame `[name, 5, 0, [cluster, incarnation, h], root, chunks]`, where h is the relay's head and
  the relay's build at h (built once per head, shared by every session). root is
  `paged::state_commitment` of the build: the root page's digest, or the empty commitment with no
  chunks for an empty state. Receive with `receive_transfer(..., nullptr, {root, h}, n ->
  derive_segment(next_generation(none, to, Profile{}, true).id, n - 1), ...)`: the packs are the
  relay's build byte for byte (test_relay_pages, full_copy). The next next carries the cursor at h.
- Its base R is a generation the relay's page window holds (a build position below the newest
  build): a pages frame `[name, 5, R, [cluster, incarnation, w], root, chunks]` to the newest build
  w when DD4's `choose_encoding(transfer, records answer from R)` picks pages; otherwise records.
- Anything else: records, as today.

The protocol change this needs is outside both tasks' files (Blocked on).

## Blocked on

The lead (protocol header, outside this task's files): `SessionRequest` and its open parser in
`include/detent_distribution_http/session_request.hh` and
`src/detent_distribution_http/session_request.cc` need the two fields, and
`src/detent_relay/service.cc` then copies them into the job:

```cpp
// session_request.hh, SessionRequest (version 2, open only):
bool pages = false;                         // "pages": true
std::span<const std::string_view> records;  // "records": [names], strictly increasing
// session_request.cc, the open's field list: add "pages" and "records"; parse a bool and an
// array of valid names, strictly increasing, at most `limits.namespaces`.
// src/detent_relay/service.cc, after `job.compressed = command.compressed;`:
job.pages = command.pages;
job.records.assign(command.records.begin(), command.records.end());
```

Until then MirrorJob carries `pages` and `records` and MirrorService honors them, but no HTTP open
sets them.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <from `campaign brief`> |

## Result

<none yet>

## Rulings I made

- The relay builds in full (PagedSnapshotBuilder) from the records full copy it would answer at its
  head, under the agent's own name for that build, so the build is its canonical copy (D7.4: a cold
  build's files are canonical); DD2's relocation is for builds after deltas, which this relay does
  not make. It builds lazily, once per head position, on the first pages answer that needs it.
- The page window holds the relay's full builds (4 per namespace; each holds all its pages). A D6
  pages delta is to the newest build, compared against the records answer from the same base.
- A head at position 0 gets records: no pages frame can name it.
- The mirror's refusal of an inbound pages frame is a plain sync failure, not UpstreamMoved, so it
  is not retried.
