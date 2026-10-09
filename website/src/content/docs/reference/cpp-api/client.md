---
title: "Client"
---

```cpp
#include "detent/client.hh"
```

`detent::Client` owns one process's view of its knobs: the baked defaults, the
CLI overrides, the current published snapshot, and (optionally) the background
updater thread that keeps it fresh. It's neither copyable nor movable; construct
one and share it by reference.

---

## Construction

```cpp
explicit Client(InitOptions options);
explicit Client(Snapshot in_memory_snapshot);
```

The first form loads `defaults_path`, parses `cli_overrides`, publishes the
defaults as version `0`, and starts the updater thread if a source is
configured. It throws `std::runtime_error` for missing or invalid defaults and
invalid CLI overrides, and `std::invalid_argument` for invalid options such as a
non-positive `poll_interval`.

The second form builds a test client from an in-memory snapshot, with no files,
overrides or background thread.

The destructor stops and joins the updater thread.

### `InitOptions`

| Field | Type | Default | Meaning |
| --- | --- | --- | --- |
| `defaults_path` | `std::filesystem::path` | empty | Baked defaults file; empty starts with no knobs |
| `cli_overrides` | `std::vector<std::string>` | empty | `"name=value"` pairs; see [Testing](/Detent/guides/testing/#cli-overrides) |
| `source_path` | `std::optional<path>` | none | Poll this snapshot file |
| `source_poll` | `std::optional<SourcePoll>` | none | A generic polling source, e.g. `make_http_poll_source()` |
| `read_source` | `std::optional<ReadSourceFactory>` | none | A scoped read source, e.g. managed generations |
| `source_id` | `std::string` | `"file"` | Label for the source in diagnostics |
| `poll_interval` | `std::chrono::milliseconds` | `200ms` | Base poll cadence; must be positive |
| `refresh_alert_after` | `std::chrono::milliseconds` | 5 minutes | Staleness threshold for alerting |
| `max_knobs` | `std::size_t` | `100'000` | Largest accepted snapshot |
| `on_update` | `std::optional<OnUpdate>` | none | Called on the updater thread after each accepted update |

`source_path`, `source_poll` and `read_source` are mutually exclusive.
`on_update` can't be combined with `read_source`. It must be fast and must not
throw; it isn't called for the initial defaults snapshot.

---

## Reads

```cpp
Result<bool, KnobError> eval(std::string_view name,
                             std::optional<std::string_view> hash_val = std::nullopt,
                             std::optional<std::string_view> switch_val = std::nullopt) const;

Result<std::int64_t, KnobError> get(std::string_view name,
                                    std::optional<std::string_view> switch_val = std::nullopt) const;

template <typename T>
Result<T, KnobError> get_as(std::string_view name,
                            std::optional<std::string_view> switch_val = std::nullopt) const;

bool eval_or_panic(std::string_view name,
                   std::optional<std::string_view> hash_val = std::nullopt,
                   std::optional<std::string_view> switch_val = std::nullopt) const;

std::int64_t get_or_panic(std::string_view name,
                          std::optional<std::string_view> switch_val = std::nullopt) const;
```

1. `eval` evaluates a bool knob with the
   [precedence rules](/Detent/reference/evaluation/).
2. `get` returns an int knob; `switch_val` is accepted and ignored.
3. `get_as<T>` converts with `std::in_range<T>`, returning `ConversionFailed`
   instead of truncating. It works for any integer `T`, signed or unsigned.
4. The `_or_panic` forms abort on any `KnobError`.

All of them are safe to call concurrently from any number of threads.

---

## Scoped Views

```cpp
ClientReadView read_view() const noexcept;
```

A `ClientReadView` pins one immutable snapshot (or one managed generation) and
evaluates against it:

```cpp
auto view = client.read_view();
auto a = view.eval("checkout:new_path", user_id, group);
auto b = view.get("search:max_results");
auto c = view.get_as<std::uint16_t>("search:page_size");
```

A view is cheap to copy, remains valid across later updates and after the
`Client` is destroyed, and applies the client's CLI overrides. Reads through a
view don't increment the client's metrics.

---

## Snapshots

```cpp
std::shared_ptr<const Snapshot> current_snapshot() const;
```

Returns the whole current merged snapshot. It's meant for tools that republish
snapshots, like the agent, not for ordinary reads. A client built with a
`read_source` throws `std::logic_error` here; use `read_view()`.

---

## Health and Metrics

```cpp
HealthSnapshot health() const noexcept;
const Metrics& metrics() const noexcept;
```

| `HealthSnapshot` field | Meaning |
| --- | --- |
| `initialized` | The client finished construction |
| `source_connected` | The last source interaction succeeded |
| `accepted_version` | Version of the current live snapshot (`0` for defaults only) |
| `staleness_seconds` | Monotonic time since the last successful update |
| `bool_knobs`, `int_knobs` | Counts in the current snapshot |
| `cli_overrides` | Number of CLI overrides |

`Metrics` holds process-wide atomic counters: `reads_ok`, `reads_missing`,
`reads_wrong_type`, `reads_conversion_failed`, `snapshot_swaps`,
`refresh_failures`, `stale_updates`, and the `source_connected` flag. See
[Observability](/Detent/reference/observability/).

---

## The Data Model

```cpp
struct GroupRollout { std::uint8_t rollout_pct = 0; };
using GroupOverride = std::variant<bool, GroupRollout>;

struct BoolDef {
    bool default_value = false;
    btree::map<std::string, GroupOverride, std::less<>> overrides;
    std::optional<std::uint8_t> rollout_pct;
};

struct IntDef { std::int64_t default_value = 0; };

using KnobDef = std::variant<BoolDef, IntDef>;

struct Snapshot {
    std::uint64_t version = 0;
    std::optional<std::string> published_at;
    std::string source_id;
    btree::map<KnobName, KnobDef, std::less<>> knobs;
};
```

`Snapshot` instances are immutable once published; every update builds a new
one.
