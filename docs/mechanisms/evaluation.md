> Status: CURRENT (2026-11-11) How a knob is evaluated. Design: [knob-model.md](../design/knob-model.md), [one-knob-type.md](../design/one-knob-type.md).

# Evaluation

Two evaluators return a knob's `int64`, one per representation, in the same order:

1. `detent::detail::evaluate(KnobDef, name, hash, switch_value)` in `include/detent/detail/evaluation.hh`, over an owned definition (JSON snapshots, defaults, the frozen client source).
2. `detent::local::evaluate(BorrowedDefinition, ns, key, hash, switch_value)` in `src/detent_local/evaluation.hh`, over a definition borrowed from a paged body or a flat image record. `ReadView::try_get` calls it after the lookup.

Order:

1. A `switch_value` that names an override: a forced value answers; a group percentage ramps with its `pct`.
2. The knob-wide rollout, when the definition has one, ramps with its `pct`.
3. The default.

Ramping is `detent::detail::ramp(pct, default, rollout_value, name, hash)`, the only place the in-bucket rule lives: with a hash and `stable_bucket(name, hash) < pct` it returns `rollout_value`, otherwise `default`. A percentage is exclusive: bucket 0 to 99 against `pct`, so 0 ramps nobody and 100 ramps every hashed caller.

`name` is a callable, so the bucket name is built only when a ramp needs a bucket: the stored qualified key when the definition has one (flat layout), else `ns:key` built on the stack. Both evaluators are `inline` in headers so the read path keeps one call.
