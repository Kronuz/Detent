# dense-distribution-build--agent-pages: journal

State: working
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

The agent's half of DD3: it asks for pages, verifies a pages full copy through DD1's receiver and
publishes it with no packer; a refused transfer is asked as records next.

## Contract with relay-pages (W5 fields)

Parsed in this branch, commit Detent:9175920e (session_request.{hh,cc} and its test), exactly the
lines relay-pages' "Blocked on" asks for: `SessionRequest` gains `bool pages` and
`std::span<const std::string_view> records`, open only. The lead's integration round owns the
parser and src/detent_relay/service.cc (copy both into MirrorJob): take Detent:9175920e or drop it.

- Open: `"pages":true` (only `true`; absent is false). The receiver takes full copies as page
  transfers for the session. Relays mirroring upstream and old agents omit it: they get records.
- Open only (a next carries neither): `"records":["a","b"]`, non-empty, strictly increasing valid names. Those
  namespaces are answered with records even in a pages session (D9 fallback after a refusal).
  A name that is not followed is ignored.
- The answer: a W6 Pages frame (kind 5), `after` 0, the target cursor, root = the target tree's
  page digest (`receive_transfer`'s TransferTarget.root), chunks = DD1's `make_transfer` of every
  page in seal order (the generation's canonical copy). The agent names the generation as a full
  copy over its base (`next_generation(..., true)`), segment n as `derive_segment(id, n - 1)`.
- A Pages frame with `after` > 0 is a delta over the store's generation at `after` (same
  incarnation); the agent receives it through `receive_delta` (Detent:c88fd1d5).

## Doing

All four steps built. Steps 1-3 at Detent:8adf04ea; quest branch merged at Detent:0a8960b1
(relay-pages at Detent:68b9665d); step 4 (pages deltas) at Detent:c88fd1d5.

## Next

1. Job 261120-104134-35f3e1 at Detent:c88fd1d5 (touched tests, relay pages included). When green:
   State "ready for review"; the lead merges.
2. End to end against the real relay once the integration round wires service.cc.

## Blocked on

Nothing.

## Evidence

Task jobs (no criterion of their own; the quest's A1/A2 run on the quest branch):

| What | Job | Result |
|---|---|---|
| Steps 1-3, touched tests + relay pages, at Detent:06b9b3c4 | 261120-102303-2536af | 10/10 passed; 100K pages boot: 2,888,084 B, built hashes 0+0, 1,347 ms |
| Steps 1-4 at Detent:c88fd1d5 | 261120-104134-35f3e1 | pending |

## Result

A cold agent builds nothing: it opens with `"pages":true`, verifies a pages full copy through
DD1's receiver (`receive_image`) and publishes it through PagedSnapshotPlanBuilder with a received
image in place of the records encoder. At 100K keys (local run): 2,888,084 transfer bytes, 3 packs,
0 build hashes, 1,006 page hashes (2,835,792 B) and 3 pack hashes (2,893,328 B), each page and pack
hashed once; descriptor and packs equal to the server's records build. A 10,000-key clustered
delta comes as a 250,970 B transfer (full copy 2,837,825 B) and publishes over the base with 0
build hashes and 104 page hashes (PagedTransferPlanBuilder, SparsePlanFinalizer from a received
image). A wrong root, full copy or delta, falls back to records through the next open's
`"records"`. Docs: docs/mechanisms/paged-transfer.md ("The agent's plans"),
docs/status/namespace-s6.md (W5 fields). Open: D9's "one other source" before records is not
built (records directly); the real relay end to end waits on the integration round.

## Rulings I made

- The plan builder lives in src/detent_local (the quest's file set, outside the task's listed
  files): PagedPlan and PagedImage are friends-only, so a received image needs `receive_image`
  there. It is PagedSnapshotPlanBuilder with a second source (a received image instead of the
  records encoder), one builder for both full copies, not a parallel one.
- The agent always sends `"pages":true`: no option; servers that do not serve pages answer
  records as before.
- A refusal sends the namespace to records until a records frame publishes it; it does not first
  ask one other source (the lead's step 3), so D9's "one other source" is not built.
- `records` is open-only, as relay-pages' MirrorJob reads it: a refusal fails the response, so the
  next request is an open anyway.
- A pages delta's plan is the warm (sparse) path, not a full relocation: the new tree reuses the
  base's packs, so a delta writes only its own pages. The removal the finalizer's warm validator
  computes must equal the base walk's, so a wrong walk cannot publish.
- derived_names_grep.sh allows src/detent_local/paged_transfer.cc (transfer_segment_ids), the one
  place a transfer's segments are named.
