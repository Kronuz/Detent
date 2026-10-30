# Validated Architectural Blueprint: first-use page verification

> Status: CURRENT (2026-10-29). Conditional GO for one bounded reader experiment. Capsule adoption remains blocked; production defaults to Legacy.

## Decision

**D1: attribute first-use lazy-page verification, then permit one streaming-verification prototype if that attribution supports it.** The target is the synchronous `LazyPageAccess::resolve` → `paged::verify_page` path. This is the next component boundary, with no per-commit architecture approval. A different optimization mechanism requires a new component decision.

Reviewed the [calibration](calibration.md), [complete-component review](reader-capsule-component-2026-10-29.md), complete canonical source **Detent:30fee44**, and [managed 5M evidence](../measurements/reader-capsule-managed-5m-20261029/README.md). The private measurement uses **Detent:b02f7dc** production bodies; **Detent:95bacf5** is a test-only correction. The prior review's C4 performance condition fails. Its other qualification conditions remain independent.

## Measured evidence

The following are recomputed from the 800 rows in [result.json](../measurements/reader-capsule-managed-5m-20261029/result.json). Each row's attachment and lookup times sum exactly to its first-read time. Columns are separate medians, which need not sum.

| Publisher / resources | Below 1 ms | Attachment median, µs | Lookup median, µs | First-read maximum, µs |
| --- | ---: | ---: | ---: | ---: |
| Live / private | 199/200 | 414.086 | 284.479 | 1126.646 |
| Live / canonical | 200/200 | 408.378 | 287.503 | 858.338 |
| Stopped / private | 198/200 | 408.269 | 377.305 | 1256.620 |
| Stopped / canonical | 193/200 | 408.508 | 383.101 | 1333.923 |

All 800 values and 5M health counts pass; major faults are zero. Both supervised publisher exits return zero. Canonical native qualification reports 230 passes and one privileged skip. These establish correctness evidence, without satisfying latency or privileged qualification.

The worst row, stopped/canonical round 2 case 33, spends **604.624 µs attaching and 729.299 µs looking up**. Its excess is 333.923 µs. Both phases contribute to failures; neither these counters nor sequential publisher states establish a cause. Any claimed margin recovery needs new paired evidence.

## Source basis and hypothesis

Source references below resolve within the [canonical source](../../.scratch/reader-capsule-20261029/canonical-source/).

1. `src/detent_local/paged_read_state.cc:18-30` resolves both the definition and namespace watermark. `lazy_page_access.cc:89-112,132-167` opens and trusts missing mappings, checks reference identity on cache hits, verifies uncached pages, validates semantics, then remembers success. First-lookup time therefore includes filesystem work, hashing, allocation and traversal.
2. `paged_layout.cc:161-174` physically hashes each uncached page, value-initializes `array<Child,256>`, decodes children into it, then traverses them again for the logical hash. `paged_page_validator.cc:92-129` already processes children incrementally. The staging array is a concrete opportunity; its emitted cost and contribution remain **unmeasured**.
3. `prepared_reader.cc:110-223` retains trusted attachment and lease ownership. `generation_file_access.hh:32-114` retains full ancestry traversal. Earlier metadata attribution measures substantial directory-trust CPU, but does not measure removable cost for this capsule build. Attachment receives coarse control counters only in this component.

**Hypothesis:** removing child staging and repeated traversal reduces first-use verification cost without changing accepted bytes. No speedup is asserted. A SHA256 pin update is outside scope: **sha256:2bb34f0** predates hardware kernels, but this i9-9980HK lacks SHA-NI ([host note](../../../Kronuz/sha256/AGENTS.md)); a hardware-kernel bump supplies no established benefit here.

## Mechanism and experiment budget

1. **One attribution batch, at most 15 minutes after setup.** Use saved case indices **13, 16, 18, 24, 27, 32, 33, 38**, covering every failing key, plus controls **0 and 39** from the [unchanged corpus cases](../../benchmarks/prepared-pack-access-5m-20261025.json). Cross ten keys with both closures, live/stopped publisher states, and observer off/on: 80 measured processes, each with its matching separate warmup. Counterbalance closure/observer order; record state order without attributing a causal effect to it.
2. **Observe existing operations.** Use preinitialized fixed storage and per-page counters, preserving `errno` and outcomes. Record mapping opens/trust checks, mapping/identity checks, cache hits/misses, page kinds/lengths/child counts, physical-hash bytes/time, child decoding/staging, logical hashing, semantic checks and traversal residual. Separate nested totals; do not sum them twice. Count definition and watermark paths. Overflow invalidates observation. Compare allocation counts, syscall counts, verification bytes and values off/on; report timing overhead. Do not add per-child clocks or extra validation to timed production work.
3. **One conditional prototype.** Proceed only if attribution and generated-code inspection identify material child-staging cost; total hashing time alone is insufficient evidence. Stream decoded children through the existing logical framing, enforcing every child/reference check and strict label order before accepting the digest. Factor shared private framing/ordering helpers where needed; retain the public interfaces. Keep the incremental validator's byte/row budget behavior. Do not instantiate its heap-owning state machine in synchronous lookup. Keep physical hashing and semantic validation complete before caching or exposing bytes.
4. **One paired screen, at most 15 minutes.** Baseline/candidate × ten keys × two closures × two publisher states × two counterbalanced repetitions gives 160 measured fresh processes plus matching warmups. Use observer-free timing binaries and a separate diagnostic binary for counters. In every closure/state, require median paired wall and caller-CPU savings to exceed their respective median absolute differences between baseline repetitions. Require every candidate sample below **950 µs**. This proposed screening margin does not replace C4. If attribution, correctness or the screen fails, preserve the negative result and stop this component. No alternative prototype or unchanged retry follows.

Before either batch, allow one harness smoke of at most three minutes. Freeze source, compiler, dependency/archive/binary hashes, path depth, corpus selection and publisher configuration. Build/test under `quiet light` with at most four jobs; measure under `quiet exclusive`. Preserve raw rows and actual stage durations. Work is serial.

## Done criteria and security review

1. **K1: Attribution is auditable.** Required counters are present and nonzero where applicable; observed work matches controls. Report per-key costs and unexplained residuals. Instrumented timings cannot qualify latency.
2. **K2: Validation remains equivalent.** Differentially compare baseline/candidate on valid pages and existing malformed/resealed fixtures, including unknown framing, truncated bodies, reserved bytes, duplicate/reordered labels, invalid child references, maximum fanout, reference overflow and custom limits. A skipped unselected-child check would silently accept malformed content; every child must still be checked. Replay decoder corpora and run focused sanitizers before screening; a winning candidate receives one CPU-hour of differential verifier fuzzing with a hang timeout before acceptance. Calibration requires limits at actual consumers, plus explicit ownership/move checks for any helper retaining member addresses.
3. **K3: Reader state remains equivalent.** Exercise disabled/pressured verification caches, changed references, sticky verification failure, namespace revision checks, retained views across republish and retirement. Preserve all pathname/UID/mode/ACL checks, held/named identities and lease retention. Charge any new storage to existing accounts; compare teardown in isolated child accounts. No trust cache, eager warmup inside the measured process, checksum substitution, publication change or dependency bump is allowed.
4. **K4: Margin is demonstrated.** Record stack/text size, allocations, minor faults, RSS and repeated-lookup cost alongside the passing paired screen. Then run exactly one unchanged uninstrumented acceptance batch: **200/200 resident-data first reads below 1 ms in each closure and publisher state**, on the real 5M corpus. Any failure keeps adoption blocked. Complete-source correctness and supported-platform requirements still apply.

## Limits and status

Streaming uses constant scratch space while work remains proportional to visited page bytes and children; page size remains 64 KiB and fanout 256. It cannot remove attachment syscalls or guarantee scheduling tails. A ten-key screen cannot establish the forty-key gate, and a small staging saving may be insufficient. No cost or power reduction is established.

This blueprint is designed and unimplemented. **Not run:** builds, tests, fuzzing, benchmarks, privileged probes or new host measurements. Only source inspection and arithmetic over preserved rows occur in this review. The separate GCC readiness-log failure is outside scope.
