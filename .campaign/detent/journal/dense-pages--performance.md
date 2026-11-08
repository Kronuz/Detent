# Dense pages performance

State: blocked on the lead: first-read acceptance fails its gate

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
5. Job 261106-112936-d91c6d passes that build/export/public-reader smoke at
   Detent:c7438bc. Its output keeps `fullbuild`, `prepared_client_probe`,
   `mixed_oracle`, `store-100k`, `store-5m`, and both export logs. No timing
   claim uses this light job. Resume after the job completes, inspect every
   stage, and use its immutable store paths in the strict smoke.
6. The 5M actual build equals 162,367 pages, 315,432,480 page bytes,
   302 packs and 315,759,038 pack bytes. Construction hashes 306,989,416
   page bytes / 162,367 calls, 315,759,038 pack bytes / 302 calls and
   22,064 metadata bytes / 303 calls. Light-job CPU and wall are informational.
7. Merge Detent:1f8436d carries only additional test qualification beyond the
   starting production code. The current owner plan parks S6. Current public
   Client and pinned ReadView are the reader baseline; the nonexistent S6 C
   ABI and reader_bench are stale assumptions, not additional prerequisites.
8. The exporter also supports 1 or 1,000 namespaces by retaining all five
   million actual definitions in `primary` and adding 999 namespaces of 100
   integer definitions when requested. The all-definition walk counts distinct
   physical pages across the production definition and watermark paths.
9. The measurement runner records binary hashes and architecture, exact-value
   oracle results, first-process warmups, resident-file warmup, A/A runs and
   counterbalanced baseline/candidate order. First reads use forty seeded
   integer keys and five rounds, with every checked value retained.
10. At Detent:935f34b, job 261106-113833-59651a builds the tools in the
    dense-release slot, clones the baseline stores without touching originals,
    derives both namespace shapes, and counts actual 5M page visits. Its
    outputs contain `baseline/` and `candidate/` tools and stores.
11. Strict smoke 261106-113849-8b5e41 depends on that preparation. It checks
    100K A/A and candidate warm reads plus forty 5M first-read keys in every
    available namespace shape. Exclusive first-read job 261106-113856-eac55c
    and warm-comparison job 261106-113856-ad9732 depend on the smoke. The
    first-read job uses 200 samples per stopped-publisher arm. The warm job
    uses 1M calls and a seeded 1M-key sample, first A/A then four whole
    baseline/candidate runs in counterbalanced order. All runs use the named
    actual mixed recipes and validate every queried value.
12. Ruff format/check and Pyrefly report zero issues in the Python runner.
    Compiler qualification is pending the preparation job. Existing fuzz
    holds remain respected; no direct measurements bypass the queue.
13. Preparation 261106-113833-59651a fails compilation because the counting
    access calls subspan on OwnedBuffer's string view. The probe now forms
    a byte span explicitly. All dependent measurement jobs are skipped;
    none provides results. Resubmit the same pipeline at the corrected code.
14. Corrected preparation 261106-113938-0f7177 runs at Detent:2a0968f.
    Strict smoke 261106-113947-a8ff2a depends on it; exclusive first-read
    261106-113948-3d61d0 and warm comparison 261106-113949-c5749a depend on
    the smoke. Resume from these four jobs, not the superseded failed chain.
15. Resume at Detent:1d2b4c0 includes the bounded overlay fix and the lead's
    reader-runner corrections. The lead replaces a refused symlink fixture
    stage with real cloned directories in 261106-132438-20983c. Functional
    strict smoke 261106-132450-077e0a passes; its latency scores fail:
    baseline 4ns 16/40 below 1 ms, median 1,016,658 ns; candidate 4ns 23/40,
    median 948,406 ns; candidate 1ns 32/40, median 835,228.5 ns; candidate
    1000ns 16/40, median 1,041,923.5 ns. Every arm has zero major faults.
16. Exclusive A/A/interleaved 261106-132451-040107 is running; exclusive
    first200 261106-132451-1f5ce0 is queued. These determine whether the
    smoke scores reproduce. No production optimization follows from smoke
    outliers alone.
17. The prepared-client probe adds an explicit profiling mode. The existing
    lazy-page qualification hook times segment miss through map/publication,
    and repeated independent Client constructions provide enough samples for
    macOS stack sampling. Profiling data is diagnostic, not gate evidence:
    repeated attachments share the process runtime and include sampling cost.
18. Owner parks new work for the Claude handoff. Detent:ff1658d commits the
    test-only profiling probe; build 261106-132922-093a04 is queued in
    dense-release. No sampling job is submitted and no production source is
    changed by this diagnostic step. Stop new experiments and let the lead
    drain the existing bounded jobs.

## Handoff

1. Branch `quest/dense-pages--performance`, worktree
   `/Users/kronuz/code/Kronuz/Detent-wt/dense-pages--performance`.
   The feature branch was fast-forwarded through Detent:1d2b4c0 before the
   test-only profile commit. No production optimization is present here.
2. Retain warm-comparison job 261106-132451-040107 and first200 job
   261106-132451-1f5ce0. Both run at Detent:a729b77. Their raw results are
   under `~/.scratch/campaign/jobs/<id>/a1/out/results/`. The latest real
   candidate stores and binaries are in job 261106-132438-20983c outputs;
   baseline stores and binaries remain in 261106-113938-0f7177 outputs.
3. Diagnostic build 261106-132922-093a04 runs at Detent:ff1658d and copies
   its binary to `JOB_OUT/probe`. The explicit command is
   `probe --profile DIR KEY EXPECTED REPETITIONS`; output adds segment-map
   duration through the existing hook. A future isolated macOS `sample`
   run can attribute repeated attachment stacks, but is not yet queued.
4. The current public Client baseline also misses the 1 ms target in the
   strict smoke. `reader.md` describes that pre-existing failure and makes
   T2 a target of the broader S6 core, absent from current main. The owner
   parks S6 for this Dense-only work; Claude owns that broader plan and must
   reconcile the Dense reader gate with S6. This task neither waives the
   threshold nor attributes baseline misses to Dense. Do not report smoke
   or repeated-process profiling as first-read acceptance.

## Open gates

1. Current isolated public-reader smoke and quantitative measurements are pending.
2. Record A/A spread before four interleaved baseline/candidate repetitions.
3. Preserve named host, architecture, toolchain, corpus hashes and exact
   commits. Published comparisons require exclusive holds.
4. Repeat accepted reader results on main after landing; task-branch results
   cannot fulfill that part of A15.
5. Running-publisher first reads and current-API 8-thread/allocation/syscall
   checks are not established by these jobs. They remain explicit gaps.

## Owner park, 2026-11-06

The owner requests a takeover checkpoint to conserve credits. No new work is started. The remaining benchmark, first-read, real-delta smoke/full and profile-build jobs are cancelled; partial output is retained but does not establish acceptance. Campaign status reports zero queued/running jobs and quiet holds are drained. Resume from the home checkout handoff docs/handoffs/dense-pages-2026-11-06.md, preserving the Claude lead's broader campaign scope.

## Owner resumption, 2026-11-06

The resumed profile build261106-173309-40965c passes. New first-read acceptance261106-173358-3b82ed uses the preserved real directories and final production binaries. Component diagnostic261106-173446-10c88a runs30 independent Client constructions per shape in one process and establishes attribution only.

## Recorded 2026-11-08

261106-173358-3b82ed failed the 200/200 gate: below 1 ms baseline 90, candidate 5M 115, 1 namespace 137, 1,000 namespaces 79 of 200 (publisher stopped, zero major faults). Attribution 261106-173446-10c88a: first live read 542 us (1 namespace), attach 342 us, lookup 201 us, mapping 121 us; diagnostic only.
