# Prepared nonfull-leaf insertion: validated architectural blueprint

**Date:** 2026-10-22. **Status:** Corrected implementation receives source and measured-cost GO at `cbc3576`; immutable CI and full product adoption remain open.

## Decision and evidence

The first design replaces a leaf even when it has room. It passes its initial functional fixture but increases synthetic mutation CPU by 28.1% and flat node allocation requests from 16,587 to 37,118. Keep its guarded construction for generic throwing-move types, then use bounded in-place construction for eligible types. The [rejected prototype and measurements](../benchmarks/btree-nonfull-replacement-20261022.json) remain reproducible. Land the independently reproduced append-size correction separately.

## Scalability

Prepare incoming arguments and every fallible shifted-key copy before touching original values. Starting at the last occupied slot, construct an actual destination value in the empty next slot, destroy the source and continue backward through the resulting hole. Finally construct incoming at its destination and publish count and size. Commitment performs no allocation, comparison or throwing construction.

Maps require copyable keys and nonthrowing key and mapped moves. When key copying is nonthrowing, copy directly during commitment without a key bank. Otherwise prepare copies only for shifted keys. Sets require nonthrowing key moves and no key bank. Keep append direct. Generic throwing-move types use the same guarded replacement mechanism as compact-root growth.

## Edge cases

Prepare map arguments in real mutable key and mapped objects. Support two-member and piecewise-tuple construction structurally, and recognized pair arguments with the constness and value categories selected by existing C++17/20 construction. Reference-valued maps and reference-element pair arguments use guarded construction. Const rvalue pairs follow the existing const-reference constructor route. Check direct key/mapped construction viability only inside the already eligible branch, and preserve the original pair constructor through guarded fallback when direct materialization is unavailable. Other forms, including custom conversions to the public const-key pair, retain their original forwarded arguments. Avoid broad pair-construction traits that could change overload acceptance or trigger eager nested allocator machinery.

Preparation failure preserves original values, topology and counts, apart from explicit caller argument consumption. Copyable generic replacement preserves originals; throwing move-only mapped values may remain moved-from. A consuming noncopyable set-key failure clears the original tree canonically. Contained lvalue arguments must be materialized before their sources move.

## Dependency and state audit

Use one nonfull-insertion entry point with compile-time materialization policies. Construct actual public value objects through the existing complete-object lifetime helpers. Never reinterpret a mutable pair as a const-key pair or use inactive union members as writable aliases.

Bound automatic scratch by the existing target-node byte budget. Use overflow-safe division before forming the worst-case shifted-key bank, then check the actual combined scratch type, including internal and inter-member padding. Choose guarded replacement when it does not fit. Key storage is aligned and uninitialized, with a constructed-prefix guard and globally qualified placement construction through a void pointer. Do not default-construct keys. Instantiate scratch only in the selected branch. Ownership moves are a separate component and are not a prerequisite or an assumed capability.

## Security and qualification

1. Prove selected traits and forwarding behavior for custom conversions, piecewise arguments, aliases, nondefault allocators, alignment and oversized fallback.
2. Refuse every preparation allocation, key copy and incoming construction. Check unchanged topology and exact conservation. Deny allocation during commitment and require success.
3. Cover every nonfull position in root/nonroot leaves, both orders and duplicates; retain generic replacement refusal and recovery coverage.
4. Run native, sanitizer and mixed-mode profiles on macOS, Linux and FreeBSD, plus the composed publication/retry/diff-cache leak gate.
5. Compare against the current independently repaired btree, including complete string heaps, node and scratch allocations, retained/peak bytes and CPU. Require benefit on changed operations without accepting another whole-workload regression. Complete the actual 100k-knob/three-override comparison before a container-selection decision.

Full-node split, promotion, rebalance and erase remain separate release findings. The [release risk evaluation](BTREE_RELEASE_RISK.md) tracks them. The older standard-map comparison needs a refreshed baseline before it can support a selection decision.

## Component results

The [R4 report](../benchmarks/btree-prepared-insertion-20261022.json) records 24 layout profiles, 24 complete-heap commitment profiles and 96 mixed profiles across the three platforms, three composed Linux sanitizer tests and thirteen actual-definition nonfull positions with sixteen allocation refusals each. Source review closes constructor forwarding, class allocation lookup, mapped-reference and exact scratch-padding findings.

Extended mutation medians are 5,245.777 and 5,260.567 ms (+0.282%). Explicit safety costs remain +6.83% for long-key insertion and +32.6% for the generic throwing-move fixture. Actual 100k-knob/three-override model medians range from 1.60% lower to 0.25% higher, with identical retained requested bytes. Random-long construction makes 201,545 fewer requests and requests 20,344,136 fewer cumulative bytes, with a 1,760-byte higher temporary peak. These instrumented samples establish neither statistical equivalence nor a general speedup. The short pruned-copy timing gap does not reproduce in the longer run; both reports remain recorded.
