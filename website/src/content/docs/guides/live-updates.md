---
title: "Live Updates"
---

A client with only baked defaults changes configuration on restart. Give it a
**source** and it changes configuration while running. A client consumes
exactly one source: a local file, or a poll function you supply.

---

## How Updates Flow

```d2 alt="A source feeds candidate updates to the client's background thread, which validates them, merges them over baked defaults and atomically publishes a new snapshot that readers load"
direction: right
source: "Source\n(file or function)"
updater: "Background updater\n(validate, version-gate,\nmerge over defaults)"
snapshot: "Current snapshot\n(atomic pointer)" {shape: cylinder}
readers: "Your threads\n(get)"
source -> updater: "candidate"
updater -> snapshot: "one atomic swap"
readers -> snapshot: "one atomic load"
```

Whatever the source, the client:

1. Validates the whole candidate against the schema, off the read path.
2. Rejects it unless its version is higher than the last accepted one.
3. Merges it over the baked defaults: a knob in the live snapshot replaces
   the baked definition as a whole.
4. Publishes the merged result with one atomic swap.

A rejected or failed update changes nothing; readers keep the last known good
snapshot. A transport failure is retried with full-jitter backoff.

---

## A File Source

The simplest live source is a JSON snapshot file that some other process
replaces:

```cpp
detent::InitOptions opts;
opts.defaults_path = "defaults.json";
opts.source_path = "/var/lib/myapp/live_knobs.json";
opts.poll_interval = std::chrono::milliseconds{200};
detent::Client client(std::move(opts));
```

The client polls the file every `poll_interval` and rereads it when its
modification time or size changes. A live snapshot looks like a defaults file
plus a `version`:

```json title="live_knobs.json"
{
  "schema_version": 1,
  "version": 7,
  "knobs": {
    "search:max_results": { "default": 50 }
  }
}
```

:::hint[Replace the file atomically]{.warning}
Publishers must write a sibling temporary file, flush it, and `rename()` it over
the target. A reader that catches a half-written file rejects it as invalid
JSON and keeps the old snapshot, so it fails safe, but a partial write still
delays the update until the next change.
:::

---

## Watch It Converge

`health()` reports the accepted version and how long ago the last update
succeeded:

```cpp
auto h = client.health();
std::printf("version %llu, %.1fs since last update, source %s\n",
            static_cast<unsigned long long>(h.accepted_version),
            h.staleness_seconds,
            h.source_connected ? "connected" : "disconnected");
```

---

## Choosing a Poll Interval

The poll interval is the upper bound on how stale a client can be while its
source is healthy. Shorter intervals propagate faster and cost more polls. An
unchanged file poll is one metadata check.

| Use | Typical interval |
| --- | --- |
| Kill switches on latency-sensitive services | 200 ms – 1 s |
| General feature flags and settings | 1 – 5 s |

For many processes on one host, prefer one [agent](/Detent/guides/shell-and-agent/)
per host over one poller per process.

:::hint[Polling, not long-polling]{.info}
File sources poll on a fixed interval. A managed agent can instead
hold a long-poll subscription to the durable authority; see
[Subscription Delivery](/Detent/guides/managed-delivery/#subscription-delivery).
:::

---

## Source Selection Rules

1. `source_path` and `source_poll` are mutually exclusive; setting both is a
   startup error.
2. With neither, the client serves baked defaults only and starts no thread.
3. `poll_interval` must be positive.
4. If the source is unreachable at startup, the client serves baked defaults
   (version `0`) and keeps retrying in the background.
