---
title: "Sources"
---

A client consumes at most one live source. Each kind is selected by a different
`InitOptions` field, and they are mutually exclusive.

| Source | Field | Link |
| --- | --- | --- |
| None (baked defaults only) | none | `detent::detent` |
| Snapshot file | `source_path` | `detent::detent` |
| Any custom poller | `source_poll` = your function | `detent::detent` |
| Managed generations | `read_source` = `make_local_read_source(...)` | `detent::local_client` |

---

## File Source

```cpp
opts.source_path = "/var/lib/myapp/live_knobs.json";
opts.poll_interval = std::chrono::milliseconds{200};
```

Every `poll_interval`, the updater checks the file's modification time and
size, and rereads the whole file (up to 16 MiB) when either changed. Publishers
must replace the file atomically with write-then-rename.

:::hint[Filesystem notifications]{.unimplemented}
The specification Detent grew from describes watching the parent directory for
rename events, with 100 ms coalescing. Detent polls metadata instead, which is
portable and cheap but adds up to one `poll_interval` of latency.
:::

---

## Custom Sources

`source_poll` accepts any function with this signature:

```cpp
using SourcePoll = std::function<
    std::optional<InitOptions::SourcePayload>(std::uint64_t after_version)>;

struct SourcePayload {
    std::string bytes;
    bool is_patch = false;
};
```

The updater calls it every `poll_interval` with the currently accepted version.

| Your function | The client |
| --- | --- |
| Returns a payload with `is_patch = false` | Validates it as a full snapshot |
| Returns a payload with `is_patch = true` | Applies it as a patch onto its own last accepted live snapshot |
| Returns `std::nullopt` | Treats it as "nothing new", with no backoff |
| Throws | Counts a refresh failure and backs off with full jitter |

The client owns parsing, version checks, merging, metrics and backoff; a source
only fetches bytes. For example, a source backed by a key-value store:

```cpp
opts.source_poll = [kv](std::uint64_t after) -> std::optional<detent::InitOptions::SourcePayload> {
    auto latest = kv->get("knobs/snapshot");   // throws on connection failure
    if (!latest || latest->version <= after) return std::nullopt;
    return detent::InitOptions::SourcePayload{std::move(latest->json), false};
};
```

---

## Managed Generation Source

```cpp
#include "detent_local/client_source.hh"

options.read_source = detent::local::make_local_read_source(
    detent::local::ClientSourceOptions{.directory = generation_directory});
```

Reads the immutable generations a [managed agent](/Detent/guides/managed-delivery/)
publishes. Refresh work runs on one configuration thread; acquiring a view and
lookups need no allocation. Unchanged polls check selection metadata without
remapping. A failed refresh keeps the prior view, and `health()` reports source
connectivity.

Clients with a read source:

1. Read through `read_view()`; `current_snapshot()` throws `std::logic_error`.
2. Apply CLI overrides first, fall back to baked defaults for missing live
   keys, and never fall back on a conversion error.
3. Must be destroyed outside an event-loop turn, since destruction joins the
   refresh worker.
