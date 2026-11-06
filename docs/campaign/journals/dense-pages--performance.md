# Dense pages performance

State: waiting on job 261106-112936-d91c6d.

## Scope

Own the Dense pages A15 and T3/T5/T6 evidence on this task branch, starting
at Detent:88dc7fc. The game master owns acceptance criteria and landing.
Reader T1/T2 remain gates, including 200/200 first reads below 1 ms in each
namespace-count and publisher-state arm. A missing or failed arm stays open.

## Evidence and next step

1. Campaign status and check agree that the feature is active, with no
   overlapping task edits. This task starts with a clean worktree.
2. Prior jobs 261105-150743-b66bbc and 261105-150744-5b7a4c provide the
   model directories for the real 100K and 5M corpora. They report 3,245
   pages / 6,217,662 pack bytes and 162,367 pages / 315,759,038 pack bytes.
   These are prior structural evidence, not current reader measurements.
3. The full-build checker accepts an optional output directory. It writes
   root, descriptor, ownership, segment payloads, selected and change records
   using the existing codecs and file helper. It prints an integer key and
   expected value from the admitted corpus. Image disposal and accounting
   assertions still run after export.
4. Next job: build in the idle dense-release slot, export both stores against
   the existing model directories, prepare `.read` records, and check a real
   value through the public managed client. Build fan-out is four. Release
   uses AppleClang and READ_COUNTERS with an empty CMAKE_OSX_SYSROOT.
5. Job 261106-112936-d91c6d runs that build/export/public-reader smoke at
   Detent:c7438bc. Its output keeps `fullbuild`, `prepared_client_probe`,
   `mixed_oracle`, `store-100k`, `store-5m`, and both export logs. No timing
   claim uses this light job. Resume after the job completes, inspect every
   stage, and use its immutable store paths in the strict smoke.

## Open gates

1. Current public-reader smoke and all quantitative measurements are pending.
2. Record A/A spread before four interleaved baseline/candidate repetitions.
3. Preserve named host, architecture, toolchain, corpus hashes and exact
   commits. Published comparisons require exclusive holds.
4. Repeat accepted reader results on main after landing; task-branch results
   cannot fulfill that part of A15.
