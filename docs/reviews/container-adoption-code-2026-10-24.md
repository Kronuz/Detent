# Container adoption component review — 2026-10-24

## Verdict

**Accept the implementation boundary, conditional on clean public-pin consumer validation.** No blocking code or security defect found in the adoption changes at Detent `79e37d0`. This is one component review, not approval of each constituent commit. It does not replace the independent architecture review or release qualification.

Before landing, configure and build a clean consumer tree using the public dependency pin, run the enabled product suite, and compile the Rust shim against that tree. Existing prebuilt standalone tests cannot establish those integration properties. Include relay and authority in that validation; the CI configuration now requests both. A failure attributable to adoption is blocking.

## Scope and evidence

1. Read repository guidance and inspected the adoption delta, CMake dependency declaration and PUBLIC linkage, Rust include list, diagnostics changes, and CI arguments. Compared the reconciled implementation at `65d8cc2` directly with external `flat_map.hh` at `a9a52b02e387ab054ff38647d3f141f9f8c00593`; the local external checkout is clean and resolves to that exact pin.
2. The external header retains the reviewed implementation, builder permutation, geometric reservation, duplicate order, merge traversal, and authoritative copy-then-swap assignment fix from `1234d08`. Functional extraction differences are comparator-dependent move `noexcept` and a nonthrowing-comparator-swap constraint on copy assignment. Namespace changes from `detent` to `kronuz`; the nine-line Detent header imports names only.
3. Ran the existing external `.scratch/build/flat_map_test` and `flat_map_comparators` executables successfully under `quiet light detent-adoption-review`, after `quiet check` showed the other light product-suite hold. The general regression printed `PASS flat_map`; both processes exited zero. No compilation or benchmark occurred. Binary/source correspondence was not independently reconstructed.
4. Read the prior contributor code review, including its CR1–CR4 findings. Its 211-test and differential-probe results belong to that earlier contributor review, not this review. The concurrently running 224-test qualification is separately owned; no outcome or count from it is claimed here. Public seven-job library CI status was supplied by the lead and was not independently verified in this pass.

## Boundary assessment

1. **Ownership and dependencies:** the definition now has one owner in `Kronuz/flat-map`; aliases do not duplicate implementation. CMake fetches an immutable full revision and links the header-only `flat_map::flat_map` interface target PUBLIC, propagating its include root to consumers. The external project gates its tests/demo on top-level use and adds no dependency closure. The local pin's CMake target and header path match these uses. Historical btree investigation sources remain outside the active consumer linkage; this review does not require removing research artifacts.
2. **Rust include closure:** `build.rs` replaces `_deps/cpp_btree-src` with `_deps/flat_map-src`, matching FetchContent's declared name and the dependency's root-level header. The wrapper's `<flat_map.hh>` resolves through that explicit root, rather than recursively including `detent/flat_map.hh`. No new archive is needed for a header-only target. This establishes consistency by inspection, not successful Rust compilation.
3. **Exception safety:** Detent's default comparator retains nonthrowing moves and swap, so its copy assignment still constructs the entire replacement before commit. Allocation/copy failure leaves the target unchanged. The new comparator constraint prevents promising that guarantee with a throwing comparator swap. Builder sorting performs fallible allocation/comparison before its nonthrowing permutation. Existing `insert_or_assign` mapped assignment retains the mapped type's guarantee; extraction does not strengthen it. `erase_if` still intentionally terminates for a throwing predicate, as the external contract states.
4. **Security and diagnostics:** no new input parser, network surface, mutable dependency revision, or executable download is introduced by flat-map adoption. Failure diagnostics read fixture-owned logs, with 64 KiB per log; the shell fixture reads a single known log, while the CMake fixture enumerates matching logs. These are test diagnostics, not production logging. The CMake enumeration is not an aggregate byte cap, but fixture ownership makes that nonblocking here.
5. **CI:** the workflow enables the shared workflow's LLVM opt-in and relay path. Its reusable workflow remains referenced at `@main`, so the reviewed shared-workflow commit is not a reproducible pin. This is an existing workflow policy, separate from the immutable flat-map source pin; successful runs must identify the workflow revision actually executed.

## Nonblocking caveats

1. Namespace ownership changes mangled names of public model container types. Source aliases preserve source spelling, not ABI compatibility with previously compiled Detent consumers. Rebuild the library and consumers together; do not describe this extraction as binary-compatible.
2. The generic library permits throwing comparator moves. Its defaulted move assignment moves vector storage before comparator assignment; if that comparator assignment throws, no strong ordering-preservation guarantee follows. This does not affect Detent's `std::less<>` instantiations and is not a regression introduced by the consumer alias. The conditional `noexcept` correctly avoids unconditional termination but is not itself an exception-safety guarantee for arbitrary comparators.
3. No performance improvement is established by extraction. Existing measurements concern the contributor's container/model work and retain their original revisions and attribution.

## Limits

No clean FetchContent configure, product build, full product test suite, Rust build, sanitizer run, public CI inspection, or benchmark was performed in this pass. Those results must be recorded separately against the final consumer candidate. No production files were edited.
