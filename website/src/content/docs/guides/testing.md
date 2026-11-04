---
title: "Testing"
---

A reader has no override path: tests see values the way production does, from
a store. That leaves two deterministic setups.

---

## Fallbacks Only

Open a reader over a store directory that doesn't exist. It opens, and every
lookup returns the fallback you pass with `DETENT_UNAVAILABLE`, so the test
exercises exactly the defaults written at each call:

```cpp
detent::Reader reader((test_dir / "no-store").string(), getuid());
assert(reader.get("search:max_results", 25) == 25);
assert(reader.lookup("search:max_results").status == DETENT_UNAVAILABLE);
```

---

## Published Values

`detent::testing::GenerationWriter` (`detent_testing/generation_writer.hh`,
CMake target `detent::testing_store`) writes real generations into a store
through the agent's own publication path. Each `publish` replaces the whole knob
set and returns the next revision:

```cpp
#include "detent_testing/generation_writer.hh"

detent::testing::GenerationWriter writer(test_dir / "store");
writer.publish_json(R"({"schema_version":1,"knobs":{
  "search:max_results":{"type":"int","default":3},
  "payments:new_processor":{"type":"bool","default":false,"rollout_pct":10}}})");

detent::Reader reader((test_dir / "store").string(), getuid());
assert(reader.get("search:max_results", 25) == 3);
```

`publish(const detent::Snapshot&)` takes the same knobs as a
[`Snapshot`](/Detent/reference/cpp-api/). Publishing again moves every open
reader to the new revision; their lookups notice it by themselves.

The writer is test-only: it's built whenever the managed agent is (the
default), is never installed, and must never be linked into a reader. Its C
face,
`detent_testing/generation_writer.h`, is what the Python and Rust tests load.

:::hint[Where the store lives]{.caution}
The reader refuses a store with a group- or world-writable ancestor, so a store
under `/tmp` fails with `DETENT_UNTRUSTED`. Put test stores under your build
tree; Detent's own tests use `${CMAKE_BINARY_DIR}/.scratch`.
:::
