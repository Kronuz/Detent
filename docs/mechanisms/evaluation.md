> Status: CURRENT (2026-11-03) How a bool knob is evaluated. Workstream: reader-s1-s4. Design: [reader.md](../design/reader.md) D6.

# Evaluation

`detent::local::evaluate(definition, ns, key, hash, switch_value)` in `src/detent_local/evaluation.hh` is the one evaluator over a borrowed bool definition. `ReadView::try_eval` calls it after the lookup and the type check.

Order:

1. A `switch_value` that names an override: a forced bool answers; a group rollout answers `stable_bucket(name, hash) < pct`, or the default when there is no hash.
2. The global rollout, when the definition has one: `stable_bucket(name, hash) < pct`, or the default when there is no hash.
3. The default.

`name` is the definition's stored qualified key when it has one (flat layout), else `ns:key`, built on the stack. A percentage is exclusive: bucket 0 to 99 against `pct`, so 0 ramps nobody and 100 ramps everybody.

The function is `inline` in a header so `try_eval` keeps the code it had. It takes `ns` and `key` rather than one qualified name because the bucket name is built only when a ramp needs it.
