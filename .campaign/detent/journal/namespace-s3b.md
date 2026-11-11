# namespace-s3b: journal

State: working
Quest: .campaign/detent/quests/namespace-s3b.md (the lead's; never edit it)

## Doing

Merging main (Dense pages, NodeAt, S1/S2, S5.1, agent store) into the branch; local build of the
merge, then evidence jobs.

## Next

1. Local build and the touched tests at the merge; commit the merge.
2. Commit the mutant patches (`tests/detent_delivery/mutants/s3b-*.patch`, runner
   `tests/detent_delivery/repair_mutant.sh`) and submit one `--fails-without` job each.
3. `campaign verify namespace-s3b --for A1,A2,A3,A4,A9,A10,A11,A12,A14`; push the branch for CI.
4. Refresh the ledger's numbers (Dense pack counts) from the jobs' output.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <pending> |

## Result

<pending>

## Rulings I made

- Merge of main (2026-11-11), onto Dense pages:
  - `paged_read_state.cc`: took main's `lookup` whole. `paged::NodeAt` bounds every field it
    decodes and the walk maps any exception to `ReadSourceFailure`, which is what this branch's
    header bounds (`prefix`, `children`, `body` against the page length, v2 offsets 16-28) did on
    DTPAGE02. Those checks are dropped as Dense-provided; K2's remaining half, the broad
    `std::exception` to `SourceFailure` mapping in `ReadView::try_get` and `try_eval`
    (`view.cc`), still applies (definition decoding and evaluation run outside `lookup`) and
    stays.
  - `test_trusted_restart mapped` ported to DTPAGE03: it finds a verified page by its magic, walks
    its nodes with `NodeAt`, and rewrites a branch's child count, a leaf's body length, or a
    definition body's kind and override bytes under the mapping.
  - Pack-count floors (`>= 10`) lowered to main's `>= 3`: a Dense 100K store has fewer packs.
  - `test_paged_segment_collection`: main's per-variant account and drain, with this branch's
    variants 14-16 and the damaged-pack sweep of variant 11.
  - `VerifiedPages::clear` kept on main's nibble encoding (zeroing a word clears every start code).
- The fix round's syscall-fault probe and its 17 mutant scripts lived in a deleted worktree's
  `.scratch`. The probe is now a committed test, `test_testing_pack_scrub_faults` (production
  `pack_scrub.cc` compiled against failing `open`, `openat` and `pread` wrappers, eight cases plus
  real `RLIMIT_NOFILE` exhaustion), and every mutant is a committed patch run as a job. The read
  bounds mutant is dropped with the code it mutated (Dense-provided).
