# dense-distribution-build--agent-pages: journal

State: working
Quest: .campaign/detent/quests/dense-distribution-build.md (the lead's; never edit it)

The agent's half of DD3: it asks for pages, verifies a pages full copy through DD1's receiver and
publishes it with no packer; a refused transfer is asked as records next.

## Contract with relay-pages (W5 fields)

Parsed in this branch (src/detent_distribution_http/session_request.cc), so relay-pages need not
touch the parser; `SessionRequest` gains `bool pages` and `std::span<const std::string_view> records`.

- Open: `"pages":true` (only `true`; absent is false). The receiver takes full copies as page
  transfers for the session. Relays mirroring upstream and old agents omit it: they get records.
- Open and next: `"records":["a","b"]`, non-empty, strictly increasing valid names. Those
  namespaces are answered with records even in a pages session (D9 fallback after a refusal).
  A name that is not followed is ignored.
- The answer: a W6 Pages frame (kind 5), `after` 0, the target cursor, root = the target tree's
  page digest (`receive_transfer`'s TransferTarget.root), chunks = DD1's `make_transfer` of every
  page in seal order (the generation's canonical copy). The agent names the generation as a full
  copy over its base (`next_generation(..., true)`), segment n as `derive_segment(id, n - 1)`.
- A Pages frame with `after` > 0 (a delta transfer) is Next 2 here; until it lands the agent
  refuses it and asks records.

## Doing

Building the touched targets (.scratch/build); then the 100K pages test.

## Next

1. Test: tests/detent_delivery/test_pages_coordinator.cc, a test-local server sending DD1 transfers:
   100K cold agent from pages with `hash.built` zero and root equal to the transfer's; a wrong
   root falls back to records (next open lists the name) and publishes the same root.
2. Delta pages over a base through `receive_delta` (D9 outcomes) into a plan over the base.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <from `campaign brief`> |

## Result

<none yet>

## Rulings I made

- The plan builder lives in src/detent_local (the quest's file set, outside the task's listed
  files): PagedPlan and PagedImage are friends-only, so a received image needs `receive_image`
  there. It is PagedSnapshotPlanBuilder with a second source (a received image instead of the
  records encoder), one builder for both full copies, not a parallel one.
- The agent always sends `"pages":true`: no option; servers that do not serve pages answer
  records as before.
- A refusal sends the namespace to records until a records frame publishes it; it does not first
  ask one other source (the lead's step 3), so D9's "one other source" is not built.
