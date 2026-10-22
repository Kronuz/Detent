# Prepared nonfull-leaf insertion: validated architectural blueprint

**Date:** 2026-10-22. **Status:** Revised after the replacement prototype failed its cost gate; implementation and qualification remain open.

## Decision and evidence

The first design replaces a leaf even when it has room. It passes its initial functional fixture but increases synthetic mutation CPU by 28.1% and flat node allocation requests from 16,587 to 37,118. Keep its guarded construction for generic throwing-move types, then use bounded in-place construction for eligible types. The [rejected prototype and measurements](../benchmarks/btree-nonfull-replacement-20261022.json) remain reproducible. Land the independently reproduced append-size correction separately.

## Scalability

Prepare incoming arguments and every fallible shifted-key copy before touching original values. Starting at the last occupied slot, construct an actual destination value in the empty next slot, destroy the source and continue backward through the resulting hole. Finally construct incoming at its destination and publish count and size. Commitment performs no allocation, comparison or throwing construction.

Maps require copyable keys and nonthrowing key and mapped moves. When key copying is nonthrowing, copy directly during commitment without a key bank. Otherwise prepare copies only for shifted keys. Sets require nonthrowing key moves and no key bank. Keep append direct. Generic throwing-move types use the same guarded replacement mechanism as compact-root growth.

## Edge cases

Prepare map arguments in real mutable key and mapped objects. Support two-member and piecewise-tuple construction structurally, and recognized pair arguments with the constness and value categories selected by existing C++17/20 construction. Other forms, including custom conversions to the public const-key pair, retain guarded construction with their original arguments. Avoid broad pair-construction traits that could change overload acceptance or trigger eager nested allocator machinery.

Preparation failure preserves original values, topology and counts, apart from explicit caller argument consumption. Copyable generic replacement preserves originals; throwing move-only mapped values may remain moved-from. A consuming noncopyable set-key failure clears the original tree canonically. Contained lvalue arguments must be materialized before their sources move.

## Dependency and state audit

Use one nonfull-insertion entry point with compile-time materialization policies. Construct actual public value objects through the existing complete-object lifetime helpers. Never reinterpret a mutable pair as a const-key pair or use inactive union members as writable aliases.

Bound automatic scratch by the existing target-node byte budget. Use overflow-safe division to check incoming storage plus the worst-case shifted-key bank; choose guarded replacement when it does not fit. Key storage is aligned and uninitialized, with a constructed-prefix guard. Do not default-construct keys. Instantiate scratch only in the selected branch. Ownership moves are a separate component and are not a prerequisite or an assumed capability.

## Security and qualification

1. Prove selected traits and forwarding behavior for custom conversions, piecewise arguments, aliases, nondefault allocators, alignment and oversized fallback.
2. Refuse every preparation allocation, key copy and incoming construction. Check unchanged topology and exact conservation. Deny allocation during commitment and require success.
3. Cover every nonfull position in root/nonroot leaves, both orders and duplicates; retain generic replacement refusal and recovery coverage.
4. Run native, sanitizer and mixed-mode profiles on macOS, Linux and FreeBSD, plus the composed publication/retry/diff-cache leak gate.
5. Compare against the current independently repaired btree, including complete string heaps, node and scratch allocations, retained/peak bytes and CPU. Require benefit on changed operations without accepting another whole-workload regression. Complete the actual 100k-knob/three-override comparison before a container-selection decision.

Full-node split, promotion, rebalance and erase remain separate release findings. The [release risk evaluation](BTREE_RELEASE_RISK.md) tracks them. The older standard-map comparison needs a refreshed baseline before it can support a selection decision.
