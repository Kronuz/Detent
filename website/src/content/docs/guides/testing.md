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
assert(reader.lookup("search:max_results", 25).status == DETENT_UNAVAILABLE);
```

---

## Published Values

`detent::testing::Store` (`detent_testing/store.hh`, CMake target
`detent::testing`) writes real generations into a store through the agent's own
publication path, in `libdetent_testing`. Each `publish` replaces the whole knob
set and returns the next revision:

```cpp
#include "detent_testing/store.hh"

detent::testing::Store store((test_dir / "store").string());
store.publish(R"({"schema_version":1,"knobs":{
  "search:max_results":{"default":3},
  "payments:new_processor":{"default":0,"rollout_pct":10,"rollout_value":1}}})");

detent::Reader reader((test_dir / "store").string(), getuid());
assert(reader.get("search:max_results", 25) == 3);
```

Publishing again moves every open reader to the new revision; their lookups
notice it by themselves.

`libdetent_testing` is a development component. A project that builds Detent
in-tree links `detent::testing`. An installed Detent provides it on request:
configure with `-DDETENT_INSTALL_TESTING=ON`, then

```sh
cmake --install build --component development --prefix /opt/detent-dev
```

installs `libdetent_testing`, `libdetent_reader` and their headers, and
`find_package(Detent CONFIG)` gives the targets `detent::reader` and
`detent::testing`. A plain `cmake --install` leaves them out. Never link it into
a shipped binary. Its C face, `detent_testing/generation_writer.h`, is what the
Python and Rust tests load.

:::hint[Where the store lives]{.caution}
The reader refuses a store with a group- or world-writable ancestor, so a store
under `/tmp` fails with `DETENT_UNTRUSTED`. Put test stores under your build
tree; Detent's own tests use `${CMAKE_BINARY_DIR}/.scratch`.
:::
