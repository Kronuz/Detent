# Persistent-map adoption

**Updated:** 2026-10-28. **State:** Isolated candidate; main retains its private tree.

The authority header forwards to `persistent_map::PersistentMap`. The private tree body is removed, preserving the existing three-parameter alias and default allocation policy. No other dependency pins or authority scheduling change. The generic library adds stored comparator and standard allocator support, preserving immutable AVL paths and owner-driven collection.

The public pin is `persistent-map:938d65e`. All seven enabled library platform and ASan/UBSan jobs pass in run 40813864441. The [source review](../reviews/persistent-map-component-2026-10-28.md) approves standalone publication conditional on those checks; it does not approve consumer adoption. The earlier TSan allocation-interceptor link conflict is covered by the documented single-executor exclusion.

All enabled consumer products and benchmarks build locally with Clang 23 in Release mode. This first build uses an explicit local library source override; it does not establish a clean public-pin consumer build. Full native consumer checks, hosted public-pin checks, exact export/checkpoint bytes and matched A/A/B costs remain pending. The matched tree screen covers 1K/100K keys, 34/200-byte keys, 1/128 updates per batch and 1/16 retained versions. It is separate from whole-system capacity qualification.

Final arena release can remain synchronous. Adoption preserves that documented contract; collection budgets count nodes rather than payload bytes or time. No new reclamation worker, runtime or byte ledger is introduced.
