# Comparison of the two flat-map assignment fixes

**Date:** 2026-10-24. **Compared:** other contributor's `1234d08714a4d7063f2171b7025f76234287c1d9` (branch report tip `da4ea02`) and the lead's unpublished `f7efb55` correction, preserved at `refs/archive/container-duplicate-local-fix`. The other contributor's fix is the integration source of truth. No duplicate correction is pushed to their branch.

## Shared result

Both copy a complete temporary before swapping it into the destination. Both explicitly preserve move operations. The other contributor's `Fragile` test refuses each of eight element-copy positions, and its actual-model allocation sweep checks an unchanged destination with disjoint key ranges. This covers the original defect and is broader than the lead's initial two-element regression. Keep those tests.

`erase_if` requires a predicate that does not throw in both designs. Their explicit `noexcept` enforces termination on violation; the lead's version documented the precondition. This is a contract choice, not a missing copy-assignment fix.

## Remaining generic defects, reproduced

1. **Throwing comparator swap breaks the strong guarantee and ordering.** Their copy assignment is available even when comparator swap can throw. Set destination comparator to ascending with keys `[1,2]`, source comparator to descending with keys `[20,10]`, and let comparator swap exchange its direction flags before throwing. `flat_map::swap` changes the comparators before exchanging vectors. After catching, the destination still contains `[1,2]` but uses descending order: `ordered=0`. Binary search no longer has its precondition. The lead's copy assignment is constrained by `std::is_nothrow_swappable_v<Compare>`, so this unsupported assignment is unavailable.
2. **Move operations claim unconditional `noexcept`.** Their defaulted move constructor and move assignment declare `noexcept` even for a comparator with throwing moves. The probe's comparator move throws: their process aborts (`exit=-6`) and the map incorrectly advertises `is_nothrow_move_constructible=1`. The lead's conditional specification reports `0` and allows the caller to catch the exception. This confirms the constructor case; assignment's declaration has the same mismatch but is not separately executed by this probe.

Neither issue affects Detent's current `std::less<>` comparator. They block the unrestricted generic-library contract, not acceptance of the original actual-type assignment correction. The narrow follow-up is conditional move exception specifications and either a nonthrowing comparator-swap constraint on copy assignment or an explicit class-wide comparator restriction. Do not replace their tests with the lead's duplicate tests.

The header also says reserving capacity permits references to survive insertion. Middle insertion still shifts entries; retain the conservative invalidation contract and tell callers to re-find. Merge traversal requires compatible input ordering and callbacks that do not mutate either input.

## Reproduce

The complete standalone probe is [comparator_contract_probe.cc](../../benchmarks/container-alternative/comparator_contract_probe.cc). It depends only on the target `detent/flat_map.hh` header and the C++20 standard library. Run it against the public branch checkout:

```sh
c++ -std=c++20 -O1 -I CHECKOUT/include comparator_contract_probe.cc -o comparator_probe
./comparator_probe
./comparator_probe move
```

Observed with LLVM 23/libc++ on Intel macOS under a light host hold:

| Probe | Other contributor | Lead's unpublished fix |
| --- | --- | --- |
| Partial throwing comparator swap | Keys `1,2`, reverse comparator, `ordered=0` | Copy assignment disabled |
| Throwing comparator move | `nothrow_move=1`, abort `-6` | `nothrow_move=0`, exception caught |

Raw source/header snapshots and results remain in `.scratch/container-fix-comparison/`. No sanitizer or performance claim comes from this comparison. The separate architecture recheck records the extraction contract and btree lifetime objection; full-product qualification remains independent.
