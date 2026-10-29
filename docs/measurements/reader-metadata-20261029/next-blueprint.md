# Validated Architectural Blueprint

> Status: CURRENT (2026-10-29). T1 attribution accepted with validation limits; conditional GO for one ancestor-storage prototype. Resource adoption remains NO-GO.

## T1 recheck

The [result](result.json), observer, builder, controller, and validation hashes agree. All 80 rows cover 20 keys × private/canonical resources × observation off/on. Observed rows contain 50 stages, 880 bytes read, four metadata closes, one attempt, and no retries. Per-key phase allocation counts match off/on exactly; constructor medians are 75/79 allocations.

Caller-CPU medians confirm directory trust at 222.409/211.568 µs and five opens totaling 69.112/69.732 µs. Stat time is nested inside checks and must not be added twice. Observation raises constructor medians from 474.225/467.467 to 538.355/522.903 µs; these are attribution results, not acceptance timings. The controller checks required stages and byte counts. It does not establish an optimization win.

[Validation](validation.json) records 20 passing invocations and four privileged trust skips. Prepared-republish coverage promised by the preceding blueprint is absent. Preserve these limits and both missing-library link-failure artifacts; neither establishes a reader behavior failure. No builds, tests, or measurements run in this review.

## Decision and mechanism

**D1: store ancestor owners inline.** In [open_publisher_directory](../../../src/detent_local/generation_file_access.hh), replace only `vector<unique_ptr<RecoveryFile>> parents` with `array<optional<RecoveryFile>, 256>` and a depth counter. Reuse the existing `RecoveryFile` destructor. Each ordinary descent currently allocates one owner object, plus occasional vector growth; the fixed container removes those allocations. Their time is unmeasured, so this is a bounded candidate, not a claimed saving.

After all existing checks succeed, emplace an empty owner at `depth`, transfer the current descriptor using the existing swaps, then increment depth. For `..`, swap with the last owner, reset that slot to close the departed directory, and decrement depth. Absolute aliases repeat the same pop operation. Success transfers `current.fd` out; destruction closes every engaged ancestor slot on success or exception.

## Scalability Check

Every push consumes an ordinary component within the existing 256-processed-component cap, so depth cannot exceed 256. Check capacity before indexing. Storage is exactly `256 * sizeof(optional<RecoveryFile>)` bytes plus depth; record the actual stack-size increase for supported builds. Descriptor count remains proportional to depth. Leave pending paths, alias strings, and component parsing unchanged. Added stack initialization or faults may erase the allocation saving.

## Edge-Case Analysis

Require privileged trusted-path tests on supported platforms, including held/named replacement injection, absolute/relative aliases, repeated `..`, root exhaustion, 40/41 links, and 256/257 processed components. Exercise maximum retained depth, missing components, and exceptions with engaged slots. Check returned inode/CLOEXEC and descriptor conservation after success and failure. Run prepared-republish and retained-reader checks missing from T1, plus appropriate sanitizer coverage. A privileged skip cannot satisfy trust validation.

## Dependency & State Audit

This uses standard containers and existing RAII only. No format, public API, shared-library extension, account topology, or parallel engine changes. Keep main's prepared-reader default and private resources. Metadata publication/recovery, lease retention through view retirement, reclamation, cursor/revision checks, and explicit admission refusals remain identical. Rollback restores the original container; disk compatibility is exact.

## Security & Scale Review

Preserve every syscall in its existing order, all held/named inode comparisons, UID/type/link-count/mode/ACL checks, symlink bounds, and exception classifications. This does not revive pre-open-stat removal or duplicate-segment-stat removal. No trust cache or record consolidation is authorized.

## Bounded acceptance

One prototype screen: private/canonical × current/inline owners, the unchanged 20 keys and 5M generation, counterbalanced order, matching warmups, compiler settings, publisher state, and exclusive host hold. Confirm reduced allocations, identical values, and unchanged filesystem-operation counts. Require paired end-to-end wall and caller-CPU benefit beyond control variation in both arms. Otherwise discard; no unchanged retry or broader profiling loop.

Only a passing screen proceeds to unchanged uninstrumented acceptance: **200/200 resident-data first reads below 1 ms in each resource arm**, with existing correctness gates. Review the complete component once. No owner format/policy decision is needed for this container substitution.
