# dense-pages--reads: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Finish prepared-reader/lazy/hash-once fixture migration and composed reader, cold/warm graph, recovery and malformed-input proof.

Adopted `phase/dense-pages-reads` at Detent:7fb6574; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Bring full-build checkpoint into the task through lead-coordinated integration.
2. Complete owned test migration; run committed scoped jobs.
3. Finish decoder and warm-skip mutation/revert proof, then sanitizer jobs.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Bounded dense sources and scoped reader/page/graph fixtures pass historically. Final composed fixtures/sanitizers remain pending.

Historical ledger: `docs/status/dense-reads.md` on `phase/dense-pages-reads`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.

## 2026-11-05 resumed validation

Added a 1,000-definition prepared fixture and one-byte/one-row warm validation
checks for whole-root reuse and a rewritten root retaining external subtrees.
They compare exact values, cold/warm counts, removed/added extents, per-kind hash
counts and checksum-correct forged child length/keys/height summaries.

Baseline job `261105-150600-3eceff` runs the composed reader/page/graph/lazy/hash
suite at Detent:43334e8. Job `261105-150527-c6567d` was cancelled: campaign's
`--commit HEAD` resolved in HOME to main, not the task worktree. All subsequent
jobs pass the expanded task SHA. Lead corrected the tool and task source globs.

Pending integration: Detent:f444c98 provides the warm fixture's local_builder
link and shared disk fixture ownership3. Production ownership3 routing is the
lead's integration work. Fresh composed fixture execution follows that merge.

Added a deterministic malformed-page stream in `test_dense_reads`: raw and
checksum-correct mutations exercise first-touch and already-verified lookups,
resumable PageValidator and GraphValidator with bounded progress. Default CI
replays 64 seeded samples; `--fuzz CPU_SECONDS SEED` runs the same checks against
measured process CPU time, with assertions independent of NDEBUG.

Baseline `261105-150600-3eceff` failed building compressors before composed tests:
missing CMAKE_OSX_SYSROOT inserts SDK C headers ahead of LLVM libc++ wrappers.
The same configuration at main is queued as `261105-150836-e99580`. Subsequent
jobs explicitly set the SDK sysroot; no dependency/source patch is needed.

## 2026-11-06 bounded fixture geometry repair

Reconciled at Detent:1f866d9 with no uncommitted work. The remaining native
regressions are bitmap fixture segment aliasing and collection variant 13's
unmeasured pressure margin. The bitmap fixture now has an opt-in valid graph
with namespace leaf packs and an index pack, preserving logical rows and
rebuilding pack ownership, graph proof and trusted records. Validation pending.

Job `261106-111149-e29a65` validates the distinct-pack bitmap graph and retains
collection variant 13's original failure as a baseline. The next probe isolates
each collection variant under its own child ledger/domain and reports exact
injection, drainage and retry resident bytes; the 16 KiB margin is unaltered.

The initial node-scattering experiment is superseded: acceptance requires the
packer's canonical cluster boundaries. Bitmap fixtures now build 128 exact
values per namespace, relocate only the packer's already sealed pages, and
rewrite placement tails without changing page digests. The parked cap race
uses a namespace pack at least as large as the omitted namespace pack, and its
coverage is asserted rather than skipped. Full GraphValidator proves each
fixture before its ownership and trusted records become reader input.

The initial bitmap job stopped on a vector/string_view append conversion,
corrected without changing geometry. Collection probe `261106-111223-310964`
reproduces variant 13 with independent child accounting; its exact boundaries
are retained in that job's stderr. Canonical geometry/sweep job follows.
