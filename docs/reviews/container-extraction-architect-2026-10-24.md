# Container extraction architecture recheck

**Date:** 2026-10-24. **Scope:** Detent `59e8c8f`, descending from main `80d4071`, and btree `44cb140fe89c0fd6b75a8a900d8b3ca97ed9a377`. Source inspection only; the release qualification belongs to the lead.

**Verdict:** Reject the btree mutable-pair cast as a portable immutable-key repair. Approve the small C++20 flat-map extraction with the conditions below. Correct flat-map copy assignment before integration; extraction packaging does not need to block release qualification.

## Validated Architectural Blueprint

The reusable component owns one sorted `std::vector<std::pair<Key, T>>`, its bulk builder, and its linear merge traversal. Keep the existing nothrow value-move constraints and mutable-key API. The caller maintains key ordering; there is no cast to a const-key pair. Detent's four model fields use that single engine through an alias or forwarding include after extraction. Remove the private implementation when the external component is adopted.

Keep C++20 and standard-library dependencies only. The initial allocator is `std::allocator`; a configurable allocator, PMR ownership policy, node handles, and a replacement iterator model are separate features with no demonstrated release requirement. The builder preserves duplicate precedence and the merge preserves canonical ordering. `PersistentMap` and authority retirement accounting remain separate mechanisms with different responsibilities.

This is a supported subset of ordered-map behavior, not a promise of C++23 `std::flat_map` compatibility. Public field types and enclosing layouts change, along with concrete iterator types and invalidation. Rebuild every C++ consumer and shared-library boundary, including bindings. A later namespace alias cannot preserve the old ABI. Wire, durable, digest and shared-memory bytes must remain identical under fixture comparison.

## Findings

### CA1: btree's const-key reference remains invalid

At the reviewed btree tip, `btree/btree.h:389` returns `reinterpret_cast<slot_type&>(x).value`. Construction creates a mutable pair, and `slot_value()` launders a pointer to that mutable pair. No operation constructs the const-key pair exposed to callers. Casting through a union does not start its lifetime. The two pair specializations are different class types, not a top-level cv qualification of the same class. C++20's lifetime rules do not permit ordinary member access through an object whose lifetime has not begun. [C++20 draft, object lifetime](https://timsong-cpp.github.io/cppwp/n4861/basic.life).

`std::launder` requires an already-live object of the appropriate similar type at the address. It neither creates that object nor changes its type. Laundering the mutable slot is consistent with its construction; laundering a const-key-pair pointer instead would not repair the missing object. [C++20 draft, pointer optimization barrier](https://timsong-cpp.github.io/cppwp/n4861/ptr.launder).

The common-initial-sequence exception permits particular member reads through a standard-layout union with a suitable active member. It is not a general license to expose an inactive pair as an ordinary pair reference. It cannot cover non-standard-layout nested values. The check at `btree.h:352` accepts both pair types being non-standard-layout, since it compares the trait results for equality. [C++20 draft, class members, paragraphs 22–25](https://timsong-cpp.github.io/cppwp/n4861/class.mem#22).

The layout assertions compare offsets of `mutable_mirror` and `const_mirror`, never the actual pair member offsets. Matching sizes and alignments cannot establish those offsets, and even matching actual offsets would not establish lifetime semantics. An implementation-specific extension would require an explicit compiler contract; resemblance to code inside a standard-library implementation supplies none.

**Disposition:** Keep the [immutable-key objection](../SHARED_LIBRARY_DECISIONS.md#cpp-btree-immutable-key-audit-october-18) open. The branch's [prototype label](https://github.com/Kronuz/btree/blob/44cb140fe89c0fd6b75a8a900d8b3ca97ed9a377/docs/MOVE_RELOCATION.md) is appropriate. Its sanitizer and performance results cannot qualify this representation for Detent. Actual const-key objects with ownership transfer or properly staged relocation remain valid alternatives; neither is on the flat-map release path.

### CA2: implicit flat-map copy assignment can destroy ordering

`include/detent/flat_map.hh:283–284` receives implicit memberwise assignment: vector first, comparator second. When destination capacity suffices, vector copy assignment can assign existing pairs in place. A throwing element copy leaves a partly replaced key sequence. Nothrow moves do not protect copy assignment.

The following is a complete reproducer shape for the existing header; it is specified here for the lead to execute, not reported as a run:

```cpp
struct V {
    V() = default;
    V(const V&) = default;
    V(V&&) noexcept = default;
    V& operator=(V&&) noexcept = default;
    V& operator=(const V&) { throw 1; }
};
detent::flat_map<int, V> dst, src;
dst.reserve(2);
dst.try_emplace(1); dst.try_emplace(2);
src.try_emplace(10); src.try_emplace(20);
try { dst = src; } catch (int) {}
// An in-place pair assignment leaves dst's keys [10, 2].
```

This follows the [libc++ 20.1.8 vector assignment path](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/libcxx/include/__vector/vector.h#L926), which assigns the existing range when capacity suffices, and [pair assignment](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/libcxx/include/__utility/pair.h#L205), which writes `first` before `second`. The example satisfies the container's current nothrow-move assertions. Subsequent binary search no longer has its ordering precondition.

The existing allocation sweep uses interleaved source and destination keys that can remain sorted after a partial overwrite. It therefore does not establish the claimed sorted-target guarantee for disjoint ranges or throwing mapped assignment.

**Blocking correction:** Copy a complete replacement before committing, then swap with a nonthrowing comparator commit for supported types. Preserve the conditional `noexcept` on swap, and explicitly preserve move construction/assignment when adding a copy assignment operator. Detent's `std::less<>` satisfies the narrow commit requirement. Document or constrain throwing comparator mutation rather than promise a guarantee the component cannot provide. Add the disjoint-range regression and an allocation-failure case with long strings or nested definitions.

Land this correctness fix as a separate commit before the mechanical extraction. Preserve benchmark provenance: existing copy-construction numbers do not measure the new copy-assignment allocation policy. A copy-and-swap assignment retains both old and replacement storage until commit; account for that peak where assignment is admitted.

### CA3: extraction must state its callback and invalidation contracts

`erase_if` at `flat_map.hh:195` compacts entries before finishing its predicate calls. If the predicate throws after compaction starts, moved-from keys can remain inside the searchable range. The minimal supported contract requires a nonthrowing predicate; a documented requirement is sufficient for this scoped extraction. If throwing predicates are supported, an exception path must restore a sorted, unique map, for example by clearing it before rethrowing. No such generic guarantee exists today.

`flat_map_merge` requires both inputs to use compatible ordering, and callbacks must not invalidate either input. Its output belongs to the caller, so callback failure need not roll back arbitrary output. Builder failure or duplicate return leaves a construction-only map that must be discarded, as the existing header states. Ordinary lookup is forbidden until successful `finish()`.

The header's advice to reserve before keeping references across insertion is too broad: reserve prevents reallocation, but middle insertion still shifts entries. Keep the conservative invalidation contract and advise callers to re-find after mutation. These clarifications do not require a new iterator abstraction.

## Scalability, edge cases and dependency state

1. **Scale:** Arbitrary-order bulk construction sorts an index and then moves values once. Its index costs four bytes per entry, in addition to the vector's capacity and nested allocations; the unsorted path explicitly rejects sizes above `uint32_t` range. Sorted append avoids that index. Linear merge still requires output storage alongside the inputs. The reported 100K rows do not establish the accepted 1M → 5M → 10M capacity tiers, RSS, or durable-publication costs.
2. **Security at scale:** The geometric reserve correction at `flat_map.hh:305` removes the repeated-member exact-reserve pattern from CR1; the CLI builder removes CR3's descending-input insertion loop. Preserve parser byte/count limits and duplicate precedence tests. A sort remains a bounded-input CPU and scratch-memory cost, and positional bulk erase remains quadratic. No hash collision policy is needed for this representation.
3. **Retirement and accounting:** The four authority drains now erase the last override and retain their existing work charge. Their storage remains allocated until the definition's vector is destroyed; do not release retained-byte charges merely because its size falls. Test actual drains with allocation and comparator use denied. Whole-snapshot destruction remains linear. Bulk sorting, merging and copy-assignment peaks must fit the existing caller admission envelope; do not introduce a second resource-accounting engine inside the container.
4. **Dependencies:** The branch removes Detent's direct btree pin and link. Keep the extracted library free of Detent model, transport and accounting dependencies. Adopt one immutable public revision when ready, with a coherent dependency closure. No supported C++23 flat-map dependency is assumed, and no btree repair is required to qualify this candidate.

## Release path and done criteria

Integrate the corrected candidate after enabled-product qualification; do not wait for package extraction. The extraction can follow as a mechanical move of that corrected engine, with the private copy removed in the adoption change. If extraction is ready first, combine adoption with the public C++ rebuild to avoid an extra consumer transition. Neither sequencing choice waives the following conditions.

1. **Correctness:** Close CA2 with the failing-before/passing-after disjoint-range test and allocation-failure coverage, preserving the nothrow move traits of actual model types. Resolve CA3's supported contracts. Confirm CR1–CR4 against the final source revision without reopening the previous broad review.
2. **Enabled products:** Run the repository's complete supported product configuration, explicitly including relay, authority, distributor and managed agent, plus the consumer/binding checks. Branch CI supplies only `DETENT_BUILD_AUTHORITY=ON`; relay defaults OFF. Keep main's relay correction `c5680e4`. A green default branch suite alone does not qualify release.
3. **Failure and bytes:** Require supported Linux/macOS builds and relevant ASan/UBSan/LSan coverage, exact canonical fixtures, publication failure checks, and allocation-denied authority cleanup. Record final commits and dependency closure. An unsupported GCC 11 environment must not be silently reported as a completed Linux gate.
4. **Capacity and extraction:** Complete the already-required release capacity/admission checks, including scratch and simultaneous generations. Keep larger exploratory tiers explicitly unqualified. For extraction, require a standalone C++20 consumer build and one implementation after adoption; rerun affected checks on the adopted revision.

**Not run:** Builds, tests, probes, benchmarks, sanitizers, CI polling, or other platforms. The prior review's test counts and performance report are existing evidence, not independently reproduced here. This recheck introduces no production or dependency changes.
