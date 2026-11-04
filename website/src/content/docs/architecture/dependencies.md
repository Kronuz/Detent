---
title: "Dependencies"
---

Detent is assembled from small, single-purpose libraries, most of them
extracted from [Xapiand](https://kronuz.github.io/Xapiand/) into standalone
repositories. Everything is fetched with CMake `FetchContent`.

---

## Detent's Own Targets

```d2 alt="detent is the core; the managed delivery and local client targets build on it; detent-agent and detent are executables over them"
direction: down
detent: "detent\n(core client)"
delivery: "detent::delivery_config\n(managed delivery)"
local: "detent::local_client\n(managed generations)"
detentd: "detentd" {shape: hexagon}
detentd -> detent
agent: "detent-agent" {shape: hexagon}
reader: "detent" {shape: hexagon}
delivery -> detent
local -> detent
agent -> delivery
reader -> local
```

| Target | Depends on | Purpose |
| --- | --- | --- |
| `detent` | the core libraries below | Client, schema, evaluation, overrides, file source |
| `detent::local_client` | `detent`, local view | Managed generation reader |

`detent` deliberately has no networking dependency, and the test suites link
the daemon libraries directly instead of spawning subprocesses.

---

## Libraries

| Library | Role in Detent |
| --- | --- |
| [atomic-shared-ptr](https://github.com/Kronuz/atomic-shared-ptr) | Lock-free snapshot publication |
| [sha256](https://github.com/Kronuz/sha256) + [endian](https://github.com/Kronuz/endian) | Stable rollout bucket |
| [char-classify](https://github.com/Kronuz/char-classify) | Knob-name validation |
| [strict-stox](https://github.com/Kronuz/strict-stox) | Strict integer parsing |
| [random](https://github.com/Kronuz/random) | Full-jitter backoff |
| [compressors](https://github.com/Kronuz/compressors) | zstd and gzip encoding and decoding |
| [RapidJSON](https://github.com/Tencent/rapidjson) | Strict JSON parsing, pinned to a post-1.1.0 commit |
| [http](https://github.com/Kronuz/http) + [reactor](https://github.com/Kronuz/reactor), [http-parser](https://github.com/Kronuz/http-parser), [radix-router](https://github.com/Kronuz/radix-router) | `detentd`'s HTTP server; handlers are `task::task` coroutines |
| [task](https://github.com/Kronuz/task) | The one coroutine type, bound to Asio |
| [consensus](https://github.com/Kronuz/consensus) | Durable Raft for the authority, with journal and completion I/O |
| [logger](https://github.com/Kronuz/logger) | Daemon logging |
| [Asio](https://think-async.com/Asio/) | Networking, pulled in by `http` and `consensus` |
| [OpenSSL](https://www.openssl.org/) 3.0+ | TLS for the authority and managed delivery |

Every Kronuz dependency is pinned by commit in
[`cmake/dependencies.json`](https://github.com/Kronuz/Detent/blob/main/cmake/dependencies.json),
and CI fails if a pin differs from the pin of a library that declares the same
dependency. As of November 4 each Kronuz pin is its library's default-branch
head, outside every range its upgrade guide marks as broken. RapidJSON is pinned
to commit `24b5e7a8`, because the 1.1.0 release doesn't compile with Clang 23.
The old and new pin of every library, and what changed for Detent, are in the
[changelog](https://github.com/Kronuz/Detent/blob/main/CHANGELOG.md).

:::hint[Pinned revisions]{.tip}
A build does not change when a library's `main` moves. Publishing the pinned
set with each Detent release is on the [Roadmap](/Detent/roadmap/#build-and-release).
:::

---

## Binary Size

`detent-agent` on macOS/arm64:

| Build | Size |
| --- | ---: |
| Default (no `CMAKE_BUILD_TYPE`) | 1.15 MB |
| `Release` | 295 KB |
| `Release` + `strip -x` | 242 KB |

Most of the remaining size is RapidJSON and ordinary C++ runtime support.
