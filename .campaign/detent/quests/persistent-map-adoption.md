# persistent-map-adoption: EX-4, the authority's ordered map from persistent-map

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status persistent-map-adoption`.

## Outcome

The authority's private AVL is deleted; its header forwards to `persistent_map::PersistentMap` at a
public revision, keeping the three-parameter alias, the owner-driven queue and the default
allocation policy. Checkpoint and export bytes are identical and the short-update cost outlier is
attributed.

## Acceptance criteria

Sources: `docs/handoffs/persistent-map.md` (the release-blocker brief, 2026-10-23); `track/persistent-map-adoption:docs/status/persistent-map-adoption.md`;
`docs/EXTRACTION_CHECKLIST.md` EX-4; `docs/reviews/persistent-map-component-2026-10-28.md` (APPROVE
WITH CONDITIONS for publication; consumer recheck NO-GO).

- A1 The private tree body is gone; the authority uses `persistent_map::PersistentMap`: a build-and-grep job
- A2 [manual] `persistent-map` (`persistent-map:938d65e` or later) has its seven library jobs green at the pinned revision: the lead names the run
- A3 A clean public-pin consumer build with no local source override; `tools/check_pins.py` 0 mismatches: the `pins` gate and the `native` gate
- A4 The full native suite (234 cases on the candidate), ASan/UBSan and TSan pass: the `native`, `asan` and `tsan` gates
- A5 The checkpoint and two exports are byte-identical to the baseline's: a job
- A6 The short-update outlier is attributed to a single case and fixed, or the gap goes to the owner with numbers (consumer recheck): `exclusive` jobs
- A7 [artifact] EX-4's row updated: `docs/EXTRACTION_CHECKLIST.md`
- A8 [manual] Library side (handoff, Requested work and Acceptance): a standard ordered map as the randomized oracle with retained versions and cursors, balance, height and subtree checks after every operation, allocation failure at every point, no allocation during traversal and explicit collection, collection turns measured and final teardown limits documented, the ownership contract and comparator requirements documented: the lead names the library commit and CI run
- A9 Lookup and update CPU, allocation requests, retained and peak heap and reclamation cost are reported for increasing key counts, update batch sizes and retained-version counts, and the structural-sharing tradeoff is quantified (handoff): `exclusive` jobs

## Contracts

Consumes `persistent_map::PersistentMap`; final arena release can stay synchronous, as documented.
Authority checkpoint and export formats unchanged.

## Decisions

1. D5. `track/persistent-map-adoption` (`Detent:6bfd04f`) is a source, not this quest's branch.

## May land early

None.
