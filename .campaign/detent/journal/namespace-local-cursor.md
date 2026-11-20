# namespace-local-cursor: journal

State: working
Quest: .campaign/detent/quests/namespace-local-cursor.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Direction (lead, mid-quest)

Folded into dense-distribution-build: only its suite and CI gate landing; mutants, the crash-model
job and review run on the finished feature. That quest's contributor merges this branch. Push often;
report when the local layer builds and its tests pass.

Coordinator changes on this branch (src/detent_delivery/coordinator.cc, coordinator.hh,
configuration.cc), for the dense-distribution-build contributor to merge around:
- Flat removal: `builder` (ImageBuilder), `paged_writer`, `compare_candidate` (flat comparison),
  every `!paged_writer` branch, `LocalImageFormat`/`image_format`, the flat begin/begin_reset calls.
- One cursor: the coordinator maps v1<->W1 at its boundary (`namespace_cursor` moves from
  derived_ids into the coordinator); local builders take the W1 target from it; comparisons with
  `options.source.configured`/`reset_from` go through the mapping.

## Doing

Step 1, flat-v1 removal: ImageBuilder, ImageValidator, flat header (DTIMAGE1), CandidateFlatValidator,
flat rows/pinned source, flat recovery route, DiskStore::begin(ImageBuilder), coordinator flat paths
(`builder`, `paged_writer`, compare_candidate), `image_format` config, flat tests, docs.

## Next

1. Step 2, one cursor: Descriptor, paged::Root, ReadView, read_view.hh, TrustedIdentityReset,
   reader/client_source identity on NamespaceCursor; builders take the W1 target from the caller
   (never source->cursor()); coordinator maps v1<->W1 (namespace_cursor moves there); layout 2,
   DTROOT04 format 4 (96..144 reserved zero); DiskStore's one entry named `scope`.
2. Step 3, A1: make_disk_store refuses a table that does not decode or a selected root whose
   framing is not DTROOT04 (digest valid) once, as TableRefused in `DiskStore::refusal()`, removes
   `selected` and starts empty, so the coordinator bootstraps a full copy; tests plus mutants
   (accept layout 1; accept DTROOT03).
3. Step 4, evidence: grep job (A2), DTIMAGE1 refusal test, T7 crash model + test_derived_names
   (A3), docs/LOCAL_IMAGE_FORMAT.md DTROOT04 (A4), suite.

## Blocked on

msg-144 to the lead: (1) file set lacks src/detent_relay/bundle.cc, include/detent_relay/bundle.hh,
src/detent_agent/main.cc, src/detent_agent/managed_host.cc, tests/detent_agent/**,
tests/detent_relay/**, tools/w5/**, README.md, which must change with the local types; (2) A2's
grep needs the v1 seams allowlisted (local make_snapshot_source's v1 SnapshotSource, the testing
writer's v1 wire for the decoders, benchmarks driving the v1 decoders); recommended option A.
Work continues meanwhile.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
