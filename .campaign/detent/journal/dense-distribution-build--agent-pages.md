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
- A Pages frame with `after` > 0 (a delta transfer) is Next 2 here; until it lands the agent
  refuses it and asks records.

## Doing

Steps 1-3 built (Detent:8adf04ea), quest branch merged (Detent:0a8960b1, relay-pages at
Detent:68b9665d). Local run: test_pages_coordinator, test_namespace_coordinator, the parser and
body tests pass; 100K keys: 2,888,084 transfer bytes, 3 packs, built hashes 0+0, checked page
hashes 1,006 (2,835,792 B) and pack hashes 3 (2,893,328 B): each page and pack hashed once; 674 ms
from agent start to ready.

## Next

1. Step 4: a Pages frame with `after` > 0 over the base through `receive_delta` into a plan over
   the base: SparsePlanFinalizer starting from a received image instead of a TrieOverlay (skip
   Assemble; its removal cross-check needs the base's unreached pages from the walk). Until then
   the agent refuses a pages delta and asks records (correct, costs the update's records size).
2. End to end against the real relay once the integration round wires service.cc.

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
