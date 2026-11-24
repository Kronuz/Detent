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
                const std::vector<std::string_view>& namespaces = {},
                std::uint64_t mapping_bytes = 0);
```

| Argument | Meaning |
| --- | --- |
| `store` | The agent's store: its generation directory (`detent-agent --generation-dir`) |
| `publisher_uid` | The uid the agent runs as. Trust is never inferred from the directory's owner |
| `namespaces` | Limits the reader to these namespaces. Empty means all, including ones published later |
| `mapping_bytes` | Caps the bytes of mapped packs and their verified bitmaps. `0` means 512 MiB; below one pack and its bitmap (`DETENT_MINIMUM_MAPPING_BYTES`) the open fails with `DETENT_INVALID_ARGUMENT` |

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
std::int64_t get(std::string_view name, std::int64_t fallback,
                 std::optional<std::string_view> hash = {},
                 std::optional<std::string_view> switch_value = {}) const noexcept;

template <std::integral T>
T get_as(std::string_view name, T fallback,
         std::optional<std::string_view> hash = {},
         std::optional<std::string_view> switch_value = {}) const noexcept;

Lookup lookup(std::string_view name, std::int64_t fallback,
              std::optional<std::string_view> hash = {},
              std::optional<std::string_view> switch_value = {}) const noexcept;
```

1. `get` evaluates a knob's `int64` with the
   [evaluation rules](/Detent/reference/evaluation/), using `hash` for
   rollouts and `switch_value` for group overrides.
2. `get_as<T>` converts that value to `T`, and returns `fallback` when it
   doesn't fit. `get_as<bool>` takes only 0 and 1.
3. `lookup` returns the status with the value, for code that needs to know why
   it got a fallback.

`get` and `get_as` return `fallback` on any error: a missing knob, an invalid
name, a namespace outside the reader's list, no generation yet, or a
generation that failed verification. The default for each knob lives at the
call that reads it.

```cpp
struct Lookup {
    detent_status status;
    std::int64_t value;          // the fallback for any status but DETENT_OK
    explicit operator bool() const noexcept;   // status == DETENT_OK
};
```

| `detent_status` | Meaning |
| --- | --- |
| `DETENT_OK` | The value is in `value` |
| `DETENT_MISSING` | The generation has no such knob |
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
thread you choose. The reader never starts a thread. `refresh()` is also the only
call that reports a namespace whose new publish can't be read: lookups keep
answering its last good publish with `DETENT_OK`, and the other namespaces move
on.

`revision(ns)` returns the revision of the namespace's attached generation, or
`std::nullopt` when there is none. With `lookup().status`, it's how a process
reports what it's reading.

`native()` returns the `detent_reader*` for C callers.

---

## Example

```cpp
// The agent's --generation-dir, and the uid it runs as.
detent::Reader reader("agent-state", agent_uid);

bool killswitch = reader.get_as<bool>("checkout:killswitch_new_path", false);
std::int64_t max_results = reader.get("search:max_results", 25);
bool enabled = reader.get_as<bool>("payments:new_processor", false, user_id, user_group);

if (auto result = reader.lookup("search:max_results", 25); !result)
    log("search:max_results: %s", detent_status_name(result.status));
```

`examples/demo.cc` is a runnable version, built as `build/examples/detent_demo`
and run as `detent_demo STORE_DIR AGENT_UID`.
