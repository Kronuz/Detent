---
title: "Global Access and Testing"
---

## `detent::global`

```cpp
#include "detent/global.hh"

namespace detent::global {
void init(InitOptions options);
void shutdown();

Result<bool, KnobError> eval(std::string_view name,
                             std::optional<std::string_view> hash_val = std::nullopt,
                             std::optional<std::string_view> switch_val = std::nullopt);
Result<std::int64_t, KnobError> get(std::string_view name,
                                    std::optional<std::string_view> switch_val = std::nullopt);
bool eval_or_panic(/* same as eval */);
std::int64_t get_or_panic(/* same as get */);
HealthSnapshot health();
}
```

A thin adapter over one process-wide `Client`, for code that can't easily have
a client injected.

1. `init()` constructs the client and throws on failure, like the constructor.
   Calling it twice throws `std::logic_error`.
2. Reading before `init()` returns `Uninitialized`.
3. `shutdown()` releases the client and stops its updater; it's mainly for test
   teardown.
4. A thread-local test client, installed by `TestKnobs`, takes precedence over
   the process-wide one.

Prefer passing a `Client&` (or a narrower interface of your own) where you
can: it makes dependencies explicit and tests simpler.

---

## `detent::testing`

```cpp
#include "detent/testing.hh"

namespace detent::testing {
class TestKnobs {
public:
    explicit TestKnobs(Snapshot snapshot);
    [[nodiscard]] TestGuard set_thread_local() &&;
};

class TestGuard {   // move-only
public:
    ~TestGuard();   // restores the previous thread-local client
};
}
```

```cpp
detent::Snapshot snap;
snap.knobs["checkout:new_path"] = detent::BoolDef{.default_value = true};

{
    auto guard = detent::testing::TestKnobs(snap).set_thread_local();
    // detent::global::* reads `snap` on this thread.
    {
        auto inner = detent::testing::TestKnobs(other).set_thread_local();
        // ...now reads `other`.
    }
    // Back to `snap`.
}
// Back to whatever was active before.
```

Guards nest, are scoped to the calling thread, and restore the previous state
when destroyed, so one test can't leak knobs into another.

---

## CLI Overrides

```cpp
#include "detent/overrides.hh"

Result<OverrideTable, std::string> parse_cli_overrides(const std::vector<std::string>& raw_pairs);
```

The client calls this for `InitOptions::cli_overrides`; you can call it
yourself to validate flags early. Each entry is `name=value`:

1. `true` and `false` (lowercase only) are booleans.
2. Anything else must parse as a signed base-10 64-bit integer.
3. Duplicate names fail, naming the offending entry.

Overrides are immutable after startup. When there are none, the read path pays
one null-pointer check.

:::hint[A standard command-line flag]{.unimplemented}
Detent parses `name=value` pairs, but doesn't ship a command-line flag parser
of its own. The specification it grew from describes a repeatable
`--just-knob name=value` flag and a `--just-knobs-config-path PATH` flag that
selects the file source; applications currently wire their own.
:::
