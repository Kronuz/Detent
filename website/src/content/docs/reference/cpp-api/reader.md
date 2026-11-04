---
title: "Reader"
---

```cpp
#include "detent/reader.hh"
```

`detent::Reader` reads the generations a
[managed agent](/Detent/guides/managed-delivery/) publishes into its store
directory. It's a header-only wrapper over `libdetent_reader`'s C ABI
(`detent/reader.h`, CMake target `detent::reader`). Open one per process and
share it; lookups are thread-safe on one reader.

---

## Opening

```cpp
explicit Reader(std::string_view store, std::uint32_t publisher_uid,
                const std::vector<std::string_view>& namespaces = {});
```

| Argument | Meaning |
| --- | --- |
| `store` | The agent's store: its generation directory (`detent-agent --generation-dir`) |
| `publisher_uid` | The uid the agent runs as. Trust is never inferred from the directory's owner |
| `namespaces` | Limits the reader to these namespaces. Empty means all, including ones published later |

Opening is the only call that throws. It throws `detent::ReaderError` when the
library's ABI version differs from the header's or `detent_open` fails;
`error.status()` holds the `detent_status` and `what()` reads
`detent_open: <status name>`. A store that doesn't exist yet opens, and lookups
return the fallback with `DETENT_UNAVAILABLE` until the agent publishes.

`Reader` is movable, not copyable. The destructor closes the handle; moving or
destroying a reader must not race a lookup on it.

---

## Lookups

```cpp
bool eval(std::string_view name, bool fallback,
          std::optional<std::string_view> hash = {},
          std::optional<std::string_view> switch_value = {}) const noexcept;

std::int64_t get(std::string_view name, std::int64_t fallback) const noexcept;

Lookup lookup(std::string_view name,
              std::optional<std::string_view> hash = {},
              std::optional<std::string_view> switch_value = {}) const noexcept;
```

1. `eval` evaluates a bool knob with the
   [evaluation rules](/Detent/reference/evaluation/), using `hash` for
   rollouts and `switch_value` for group overrides.
2. `get` returns an int knob.
3. `lookup` returns the status and the value of either type, for code that
   needs to know why it got a fallback.

`eval` and `get` return `fallback` on any error: a missing knob, a wrong type,
an invalid name, a namespace outside the reader's list, no generation yet, or a
generation that failed verification. The default for each knob lives at the
call that reads it.

```cpp
struct Lookup {
    detent_status status;
    detent_value value;          // value.kind, value.boolean, value.integer
    explicit operator bool() const noexcept;   // status == DETENT_OK
};
```

| `detent_status` | Meaning |
| --- | --- |
| `DETENT_OK` | The value is in `value` |
| `DETENT_MISSING` | The generation has no such knob |
| `DETENT_WRONG_TYPE` | The knob has the other type |
| `DETENT_INVALID_NAME` | The name isn't a valid `namespace:key` |
| `DETENT_NOT_OPENED` | The namespace is outside the reader's list |
| `DETENT_SOURCE_FAILURE` | The generation failed verification |
| `DETENT_UNAVAILABLE` | No prepared generation for this namespace yet |
| `DETENT_UNTRUSTED` | Ownership, mode, ACL or ancestry refused |
| `DETENT_INVALID_ARGUMENT`, `DETENT_NO_MEMORY`, `DETENT_IO` | The call itself failed |

Any other value is an error too: the C ABI may add statuses.
`detent_status_name(status)` gives the short name (`"missing"`, `"untrusted"`).

---

## Freshness and Diagnostics

```cpp
detent_status refresh() const noexcept;
std::optional<std::uint64_t> revision(std::string_view name_space) const noexcept;
```

A lookup notices a new publish by itself, with one load per lookup, and
attaches it on that lookup. `refresh()` does the same work ahead of time from a
thread you choose. The reader never starts a thread.

`revision(ns)` returns the revision of the namespace's attached generation, or
`std::nullopt` when there is none. With `lookup().status`, it's how a process
reports what it's reading.

`native()` returns the `detent_reader*` for C callers.

---

## Example

```cpp
// The agent's --generation-dir, and the uid it runs as.
detent::Reader reader("agent-state", agent_uid);

bool killswitch = reader.eval("checkout:killswitch_new_path", false);
std::int64_t max_results = reader.get("search:max_results", 25);
bool enabled = reader.eval("payments:new_processor", false, user_id, user_group);

if (auto result = reader.lookup("search:max_results"); !result)
    log("search:max_results: %s", detent_status_name(result.status));
```

`examples/demo.cc` is a runnable version, built as `build/examples/detent_demo`
and run as `detent_demo STORE_DIR AGENT_UID`.
