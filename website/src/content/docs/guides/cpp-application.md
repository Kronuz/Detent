---
title: "Using Detent in a C++ Application"
---

This guide adds Detent to an existing C++20 program: open one reader over the
managed agent's store, pass a fallback with every read, and check the status
when a value looks wrong.

---

## 1. Add the Dependency

```cmake
include(FetchContent)
FetchContent_Declare(detent
  GIT_REPOSITORY https://github.com/Kronuz/Detent.git
  GIT_TAG        main)
FetchContent_MakeAvailable(detent)

target_link_libraries(my_app PRIVATE detent::reader)
```

`detent::reader` has no networking dependency. The values come from a
[managed agent](/Detent/guides/managed-delivery/) running on the same host,
which publishes verified generations into its store directory.

---

## 2. Open One Reader

```cpp
#include "detent/reader.hh"

// The agent's --generation-dir, and the uid the agent runs as.
detent::Reader reader("agent-state", agent_uid);
```

Open **one** reader per process and share it; lookups are thread-safe. The
publisher uid is required because the reader never infers trust from the
directory's owner: it checks the store's ownership, mode, ACL and ancestry
against that uid, and refuses with `DETENT_UNTRUSTED` otherwise.

Opening throws `detent::ReaderError` only for a store it can't trust or a call
that fails outright. A store the agent hasn't created yet opens fine, and reads
return their fallbacks until the first publish, so a process can start before
its agent.

---

## 3. Read Knobs

Every read takes the value to use when the knob can't be read:

```cpp
bool killswitch = reader.eval("checkout:killswitch_new_path", false);
std::int64_t max_results = reader.get("search:max_results", 25);
```

Pass a hash value for rollouts and a switch value for group overrides:

```cpp
bool enabled = reader.eval("payments:new_processor", false,
                           /*hash=*/user_id, /*switch_value=*/user_group);
```

A read never throws. A missing knob, a wrong type, a bad name or no generation
yet all return the fallback, so the safe default lives next to the code that
reads the knob.

:::hint[Read where you decide]{.tip}
Read the knob at the point where you branch on it, rather than caching the
value at startup. Each lookup checks whether the agent published a new
generation and switches to it, so an update takes effect without a restart.
:::

---

## 4. See Why You Got the Fallback

`lookup()` returns the status with the value:

```cpp
auto result = reader.lookup("search:max_results");
if (!result)
    log("search:max_results: %s", detent_status_name(result.status));
```

`DETENT_MISSING` usually means a typo or a knob the authority hasn't defined;
`DETENT_UNAVAILABLE` means the agent hasn't published this namespace yet. The
[Reader reference](/Detent/reference/cpp-api/reader/) lists every status.

`revision("search")` returns the revision of the namespace the reader is
attached to, or `std::nullopt`; expose it on your own status page.

---

## A Complete Program

`examples/demo.cc` reads a kill switch and an int knob, then prints the lookup
status and the namespace revision:

```sh
cmake -B build && cmake --build build
./build/examples/detent_demo agent-state "$(id -u)"
```

With the agent running as you over `agent-state`, it prints the published
values; with no store yet, it prints the fallbacks and `lookup: unavailable`.
