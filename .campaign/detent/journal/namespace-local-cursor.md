# namespace-local-cursor: journal

State: ready for the merge (local layer builds; suite 278/278 locally at fb7e2f89)
Quest: .campaign/detent/quests/namespace-local-cursor.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Direction (lead, mid-quest)

Folded into dense-distribution-build (file set src/**, include/**, tests/**, ...): only its suite
and CI gate landing; mutants, the crash-model job and review run on the finished feature. That
quest's contributor merges this branch. Push often; report when the local layer builds and its
tests pass.

## Contract for dense-distribution-build (what this branch gives the merge)

- Local types hold `distribution::NamespaceCursor`: Descriptor, paged::Root, ReadView::cursor(),
  read_view.hh, TrustedIdentityReset::configured, reader/client_source identity, recover `expected`.
- A full copy's W1 target is its input's: `ValidatedSnapshot::namespace_cursor()`. The v1 decoder
  sets it by D88 (`distribution::namespace_cursor(Cursor)`, now in include/detent/cursor.hh);
  FrameInput sets it from W6 directly. A delta's target is its base's cursor at the delta's target
  revision (the base binds the incarnation); `exact_base` checks the revisions.
- Agent store layout 2, root `DTROOT04` format 4 (cursor: position 56, cluster 64, incarnation 80;
  96..144 zero). DiskStore's one entry is `disk_store_entry` ("namespace").
- `LayoutRefused` (store_table.hh): a whole table of an older layout, or a whole root of an older
  format its entry names. make_disk_store refuses it once (`DiskStore::refusal()`), removes
  `selected`, and opens empty; collection removes the old files. A newer layout fences like damage.
- `derive_generation(profile, NamespaceCursor, base, ordinal)` hashes cluster, incarnation and
  position (agent_store.cc and next_generation both call it).
- v1 seams left for the merge to delete: `make_snapshot_source(generation, account, v1 identity)`
  (relay bundle export), the coordinator's v1<->W1 helpers (`same_identity`, `same_cursor`,
  `v1_cursor` in coordinator.cc's anonymous namespace), `distribution::namespace_cursor`.

Coordinator changes on this branch: flat removal (`builder`, `paged_writer`, `compare_candidate`,
the flat-head migration and its dead `!source` maintenance branch, `LocalImageFormat`/
`image_format`), the v1<->W1 helpers above at each identity comparison, and `pause()`: disk
progress no longer ends a poll pause (only close, a new cancellation, or `wake()` from a repair or
compaction request). Relay bundle.cc has the same v1<->W1 helpers for its v1 cache keys.

## Doing

Nothing: steps 1-3 and A4 are committed (3711080a, d99e81b1, fb7e2f89) and pushed.

## Next

1. The dense-distribution-build contributor merges this branch; the v1 seams below go with v1.
2. On the finished feature (lead): mutant for A1 (accept the old version), T7 crash-model job (A3).

## Blocked on

Nothing. msg-144 (file set, A2 seams) is superseded by the fold: the feature quest's set is
src/**, include/**, tests/**.

## Evidence

| Criterion | Evidence |
|---|---|
| suite at 3711080a (flat removal) | local run: 273/278; disk_ledger, disk_store, paged_root_reclaim, coordinator retained idle, reader-pressure (all fixed after) |
| suite at fb7e2f89 | local `ctest -j4` 278/278 passed (.scratch/namespace-local-cursor/ctest4.log); CI on push |
| A1 | test_local_disk_store `older_layouts`: layout-1 table and DTROOT03 root (from current bytes) refused once, store opens empty, publishes 43, old image collected; root damage fences; a newer table refuses to open, files kept. Mutant deferred to the feature (lead) |
| A2 | flat-v1 grep: only the D95 refusal test (test_managed_reader DTIMAGE1, whole and as framing), test_managed_config's unknown-key case and dated status/handoff docs remain. v1 `Cursor` remains only on v1 wire seams: snapshot_source/paged_snapshot_source (relay export), detent_testing generation_writer (v1 wire producer), benchmarks' v1 corpora and deltas |
| A3 | test_local_derived_names passes at layout 2; test_agent_store's pinned derivation re-pinned (generation-560af59f6e78605358004bdb201499f4). Crash model deferred (lead) |
| A4 | docs/LOCAL_IMAGE_FORMAT.md: "Local image format, version 4", root table with `DTROOT04`, Other layouts section |

## Result

<none yet>

## Rulings I made

- A full copy carries its W1 target on ValidatedSnapshot rather than as a builder argument: one
  place for the v1 decoder (D88) and FrameInput to set it, no v1 type in detent_local's builders.
- A delta's W1 target is derived from its exact base (same incarnation, the delta's revision).
- StateEquivalence compares cursor, profile and state commitment: a paged encoding is canonical.
- test_coordinator's from-flat cases become from-installed (a paged head installed before start);
  its recovery-failure case reaches maintenance recovery through a forced compaction.
- Dated records (docs/IMPLEMENTATION.md, docs/PAGED_DELIVERY_REVIEW.md,
  docs/PREPARED_ACTIVATION_ARCHITECTURE.md, docs/measurements, docs/reviews, docs/dev audit) keep
  their flat-v1 mentions; current docs and the website no longer name it.
- src/detent_agent/managed_host.cc: `require_prepared` is always true (no flat format remains).
- Only older layouts and formats are refused and removed (A1 names older stores); a newer one is not
  this agent's to delete, so it fences and keeps files (test_foreign_generation's format 9).
- Generation names hash the whole W1 cursor: a trusted reset to another cluster with the same
  incarnation and position must not reuse the selected generation's name (test_derived_names).
- Coordinator poll pause ignores disk progress: paged collection after a publication woke the 12 h
  poll pause and sent a delta poll at once (test_delivery_coordinator retained idle, stray POST).
- reader-pressure budget 5120 bytes: two paged generations peak at 4138, a third needs 5834.
