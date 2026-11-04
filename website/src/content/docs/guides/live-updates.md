---
title: "Live Updates"
---

A client with only baked defaults changes configuration on restart. Give it a
**source** and it changes configuration while running. A client consumes
exactly one source: a local file, or an HTTP endpoint.

---

## How Updates Flow

```d2 alt="A source feeds candidate updates to the client's background thread, which validates them, merges them over baked defaults and atomically publishes a new snapshot that readers load"
direction: right
source: "Source\n(file or HTTP)"
updater: "Background updater\n(validate, version-gate,\nmerge over defaults)"
snapshot: "Current snapshot\n(atomic pointer)" {shape: cylinder}
readers: "Your threads\n(eval / get)"
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
opts.defaults_path = "just_knobs.json";
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
    "search:max_results": { "type": "int", "default": 50 }
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

## An HTTP Source

A client can also poll an HTTP endpoint that serves `GET /v1/snapshot`:

```cpp
#include "detent_http/http_source.hh"

detent::InitOptions opts;
opts.defaults_path = "just_knobs.json";
opts.source_poll = detent_http::make_http_poll_source("knobs-host", 8317);
opts.poll_interval = std::chrono::milliseconds{1000};
detent::Client client(std::move(opts));
```

Link `detent_http::detent_http` for this. Each poll is a conditional `GET
/v1/snapshot`:

1. The client sends `If-None-Match` with the last ETag, and handles a
   `304 Not Modified` with no body.
2. It also sends `X-Detent-Base-Version` with its accepted version, and applies
   a merge-patch of just the changed knobs when the endpoint answers with one.
3. It sends `Accept-Encoding: zstd, gzip` and decodes the response
   transparently.

The client keeps one keep-alive connection and reuses it for every poll. See
[Sources](/Detent/reference/cpp-api/sources/#http-source) for every option.

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
unchanged file poll is one metadata check; an unchanged HTTP poll is a tiny
`304`, so its cost is mostly connection and request overhead, not bytes.

| Use | Typical interval |
| --- | --- |
| Kill switches on latency-sensitive services | 200 ms – 1 s |
| General feature flags and settings | 1 – 5 s |
| Large fleets polling one endpoint | Several seconds, or an agent per host |

For many processes on one host, prefer one [agent](/Detent/guides/shell-and-agent/)
per host over one poller per process.

:::hint[Polling, not long-polling]{.info}
File and HTTP sources poll on a fixed interval. A managed agent can instead
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
