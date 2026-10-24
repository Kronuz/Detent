# Ask: qualify an ordered-container implementation or select a replacement

**Priority:** P0 · Rank 1 of 7 · Correctness blocker.

Resolve this first: unresolved actual-type lifetime or failure-cleanup defects prevent a safe release regardless of throughput.

## Repositories and starting point

1. **Start repository:** [Kronuz/btree](https://github.com/Kronuz/btree).
2. **Consumer source and integration tests:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
3. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** `include/detent/model.hh`, `include/detent/overrides.hh`, `docs/BTREE_RELEASE_RISK.md`, `docs/CONTAINER_SELECTION_ARCHITECTURE.md` and `benchmarks/model-container-comparison-20261022.json` in the consumer repository. The recorded consumer pin is `cbc35762bc4a98ad91361be29d05f0ae715a1339`; verify actual headers and overrides.

**Dependencies and ownership:** Can start now. Own container implementation and four model fields; coordinate authority cleanup/accounting edits with the resource and persistent-map tasks.

**Release-unblocking outcome:** An actual-type counterexample or explicit closure for every recorded safety gap, followed by one measured repair/replacement decision and qualified consumer adoption. A library-only patch or ordinary passing tests do not close this gate.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Review a C++20 application using a B-tree for public model fields. The relevant shapes are string-keyed record tables, string-keyed patch tables, nested string-keyed scalar override tables, and caller-supplied override tables. Records contain scalar variants and nested tables. Some consumers publish immutable snapshots; others incrementally construct and retire definitions under work budgets.

## Problem and decision boundary

The safety questions are full-node insertion/erase involving const keys, object lifetime during relocation, and cleanup after allocation or copy failure. Determine which are reproducible for the application's actual types. Generic guarantees for arbitrary throwing user types are a separate library concern.

A transactional mutation strategy can solve lifetime problems while adding unacceptable allocation, copying and memory costs. Its adoption requires measurements. Standard ordered maps provide another safety option. A hash index may improve lookup or random construction but introduces collision policy, ordering and publication costs. No representation is assumed to win.

## Requested work

1. Reproduce each actual-type correctness gap independently, including denied-allocation paths. Separate language-level lifetime defects from stronger exception guarantees the application does not require. Retain useful isolated repairs.
2. Recover existing experiments before writing more prototypes. Compare the corrected B-tree, a standard ordered map, and a hash candidate only where applicable. Keep one production representation after selection.
3. Measure the complete lifecycle: parse, validate, construct, copy, patch, publish, transfer, prune and destroy. Include nested overrides, short and maximum supported names, random/ordered insertion, broad and hot-key reads, and misses.
4. Recommend a representation from explicit CPU, memory and safety evidence. Further structural B-tree repair needs a demonstrated constraint that rules out the simpler safe alternatives. Hash adoption requires an adversarial-key policy and sorted publication costs.

## Acceptance

Use at least a 100K-record comparison to expose index costs, then identify what larger-system qualification remains necessary. Measure allocation requests, retained/peak heap and CPU separately from RSS. Include coexisting defaults, live snapshots and histories. Charge hash publication's sorting scratch and CPU to that candidate.

Require equivalent duplicate rejection, ordering and evaluation, plus byte-identical valid serialization, command digests and mapped publication where those are compatibility requirements. Exercise allocation failure during parse/copy/patch/publication, partial discard, shared captures and retirement. Run unsuppressed leak checks on a supported platform. Leak-disabled ASan is useful memory-safety coverage, but cannot qualify leaks.

Incremental definition cleanup must retain its accounting charge until disposal completes. Positional map erase being amortized constant does not prove a strict time bound; measure maximum cleanup turns at supported override limits. Whole-snapshot destruction remains linear unless a separate mechanism budgets it.

Changing public container types changes C++ layouts, iterators and concrete-type interoperability. State that compatibility break and rebuild every affected consumer. Preserve durable bytes and wire behavior. Remove unused direct dependencies after selection; retiring the upstream container project is outside this task.

Work serially in an isolated checkout, follow repository/shared-library instructions and coordinate host load. Return a concrete decision, isolated fix commits, actual-type failure tests, reproducible before/after results and compatibility notes. If no candidate meets a budget, name the budget and measured failing case.
