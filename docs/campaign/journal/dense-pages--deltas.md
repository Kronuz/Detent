# dense-pages--deltas: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Finish composed delta/compaction fixtures and expand independent full-build comparison to the accepted seeded workload.

Adopted `phase/dense-pages-deltas` at Detent:64aab0f; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Compose actual full builders/read validators and run updated delta/compaction fixtures.
2. Exercise 1,000 seeded rounds with 1 to 500 changes and retain exact values/occurrence oracle.
3. Capture actual sparse writes and relocation accounting; prove mutants fail.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Standalone production integration: 50 seeded rounds over 2,000 definitions, path/extent ownership, inline boundaries, no-op zero writes and relocation zero page hashing; historical Debug and ASan/UBSan.

Historical ledger: `docs/status/dense-deltas.md` on `phase/dense-pages-deltas`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.

## Bounded fixture migration (2026-11-05)

Adopted the authorized fullbuilder checkpoint as Detent:9340e5a. The
changed-leaf fixture compares placement-free state commitments and dense
canonical fingerprints. Its malformed duplicate-extent parent uses explicit
version-3 codec bytes because canonical packing refuses small external pages;
the warm-validator rejection remains exercised independently of that packer.

Composed proof covers delta leaves, delta plans, overlay assembly, dense delta
assembly and partial/complete compaction. Submission follows this commit; the
job id is recorded in the next journal-only commit.

Known integration dependency: the shared disk fixture writes ownership layout
3, but this task's ownership codec still accepts only layout 2. Lead owns the
routing change (Detent:6b198c3). The compaction fixture's manual lease also still
uses layout 2; report the composed result before integrating ownership changes.
The 1,000-round expansion remains outside this bounded trial.

State: waiting on job 261105-212056-3c8d10

Proof job 261105-212056-3c8d10 is pinned to Detent:1a11408 (Apple
Clang, Debug, managed publication enabled, five delta/compaction fixtures,
maximum four build jobs). No passing composed result is claimed yet.

## Scoped proof repair (2026-11-05)

Job 261105-212056-3c8d10 failed while compiling the new malformed-page
fixture: OwnedBuffer::append takes string_view, not vector<char>. The fixture
now passes its explicit data/size span. No runtime proof resulted from that job.

Integrated the approved ownership-routing checkpoints as Detent:f70bc37 and
Detent:9258d8c. The first cherry-pick restored the integration journal absent
from this task branch; its contents remain unchanged. The owned compaction
fixture now writes paged::format_version in its manual lease, matching the
shared disk fixture and the version-3 ownership codec.

State: working; scoped proof resubmission follows this commit.

State: waiting on job 261105-213628-5a503c

Job 261105-213628-5a503c pins the repaired composed proof to
Detent:a34fe4e, reusing the Apple Clang Debug slot and the same five fixtures.
The 1,000-round workload remains deferred until scoped proof passes.

Job 261105-213628-5a503c built all four delta test targets, then failed
compiling compaction's shared publication library: paged_named_root.cc:324
still calls removed paged::physical_commitment on root metadata. This is an
integration-owned hash-domain migration, reported to the lead. No tests ran
because the build-and-ctest command stopped on that error.

State: waiting on job 261105-213737-260e38; compaction blocked on integration.

Job 261105-213737-260e38 pins the same Detent:a34fe4e code and runs the
four delta fixtures separately from the blocked publication target. No second
wait this turn. Root metadata likely needs metadata_commitment, not the
compiler's pack_commitment suggestion; integration owner must confirm.
