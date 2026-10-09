---
title: "Using Detent in a C++ Application"
---

This guide adds Detent to an existing C++20 program: ship baked defaults with
the binary, construct one client, and read knobs on the hot path.

---

## 1. Add the Dependency

```cmake
include(FetchContent)
FetchContent_Declare(detent
  GIT_REPOSITORY https://github.com/Kronuz/Detent.git
  GIT_TAG        main)
FetchContent_MakeAvailable(detent)

target_link_libraries(my_app PRIVATE detent::detent)
```

`detent::detent` has no networking dependencies. Link `detent_http::detent_http`
as well only if the process polls a distributor directly.

---

## 2. Write Your Baked Defaults

Ship a defaults file alongside your binary. It's the configuration your process
starts with before any live data arrives, and the fallback for any knob the
live snapshot omits:

```json title="just_knobs.json"
{
  "schema_version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "type": "bool", "default": false },
    "search:max_results": { "type": "int", "default": 25 },
    "payments:new_processor": { "type": "bool", "default": false }
  }
}
```

A defaults file has no `version`; it's always version `0`. Keep it in source
control next to the code that reads the knobs: adding a knob read and its safe
default in the same change is what makes a missing knob impossible.

---

## 3. Construct a Client

```cpp
#include "detent/client.hh"

detent::InitOptions opts;
opts.defaults_path = "/etc/myapp/just_knobs.json";

detent::Client client(std::move(opts));
```

Construct **one** client per process and share it. The constructor parses the
defaults synchronously and throws `std::runtime_error` if the file is missing
or invalid. That's deliberate: a process that can't load its safe defaults
should fail at startup, not serve with an empty configuration.

If you'd rather not thread a `Client&` through your code, the
`detent::global` adapter holds one process-wide instance:

```cpp
#include "detent/global.hh"

detent::global::init(std::move(opts));
bool on = detent::global::eval_or_panic("checkout:killswitch_new_path");
```

---

## 4. Read Knobs

There are two styles of read. The `_or_panic` helpers return the value
directly and abort on any error; they're the recommended default for knob names
written in your code:

```cpp
bool killswitch = client.eval_or_panic("checkout:killswitch_new_path");
std::int64_t max_results = client.get_or_panic("search:max_results");
```

The plain forms return a `Result` for code that must handle errors itself, such
as a library that can't abort its host process:

```cpp
auto r = client.eval("checkout:killswitch_new_path");
if (r) {
    use(r.value());
} else {
    log(r.error().what());   // e.g. "MissingKnob: ..."
}
```

Pass a hash value for rollouts and a switch value for group overrides:

```cpp
bool enabled = client.eval_or_panic(
    "payments:new_processor",
    /*hash_val=*/user_id,
    /*switch_val=*/user_group);
```

For a checked integer conversion, use `get_as<T>`:

```cpp
auto limit = client.get_as<std::uint16_t>("search:max_results");
// ConversionFailed if the value doesn't fit, never truncated.
```

:::hint[Reads are cheap; call them where you decide]{.tip}
A read is one atomic load, one B-tree lookup and a few comparisons, with no
lock, I/O or allocation. Read the knob at the point where you branch on it,
rather than caching the value at startup: that's what lets a live update take
effect without a restart.
:::

---

## 5. Read a Consistent Set of Knobs

Each individual read sees one complete snapshot, but two separate reads may see
two different snapshots if an update lands between them. When several knobs
must agree, pin one view:

```cpp
auto view = client.read_view();
auto max = view.get("search:max_results");
auto on = view.eval("checkout:killswitch_new_path");
// Both reads observed the same snapshot.
```

A view stays valid across later updates and even after the client is
destroyed.

---

## 6. Add a Live Source

So far the process only knows its baked defaults. To receive updates without a
restart, give the client one source, either a file it polls or a distributor:

```cpp
opts.source_path = "/var/lib/myapp/live_knobs.json";
opts.poll_interval = std::chrono::milliseconds{200};
```

```cpp
#include "detent_http/http_source.hh"

opts.source_poll = detent_http::make_http_poll_source("distributor-host", 8317);
```

[Live Updates](/Detent/guides/live-updates/) covers both in detail.

---

## 7. Check Health

`client.health()` returns a small diagnostic snapshot you can expose on your
own status page:

```cpp
auto h = client.health();
// h.initialized, h.source_connected, h.accepted_version,
// h.staleness_seconds, h.bool_knobs, h.int_knobs, h.cli_overrides
```

`staleness_seconds` is the most useful one to alert on: it's the time since
the last successful update, measured on a monotonic clock. See
[Observability](/Detent/reference/observability/).

---

## A Complete Program

```cpp title="main.cc"
#include "detent/client.hh"

#include <cstdio>

int main() {
    detent::InitOptions opts;
    opts.defaults_path = "just_knobs.json";
    detent::Client client(std::move(opts));

    std::printf("search:max_results = %lld\n",
                static_cast<long long>(client.get_or_panic("search:max_results")));
    std::printf("checkout:killswitch_new_path = %s\n",
                client.eval_or_panic("checkout:killswitch_new_path") ? "true" : "false");
}
```

```text
search:max_results = 25
checkout:killswitch_new_path = false
```

The repository's `examples/demo.cc` is a slightly longer version of this
program, built as `build/examples/detent_demo`.
