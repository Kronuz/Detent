---
title: "Dependencies"
---

Detent is assembled from small, single-purpose libraries, most of them
extracted from [Xapiand](https://kronuz.github.io/Xapiand/) into standalone
repositories. Everything is fetched with CMake `FetchContent`.

---

## Detent's Own Targets

```d2 alt="detent is the core; detent_http and detent_agent_lib build on it; detent-agent is an executable over its library"
direction: down
detent: "detent\n(core client)"
http: "detent_http\n(poll source)"
alib: "detent_agent_lib"
local: "detent::local_client\n(managed generations)"
detentd: "detentd" {shape: hexagon}
detentd -> detent
agent: "detent-agent" {shape: hexagon}
reader: "detent" {shape: hexagon}
http -> detent
alib -> detent
local -> detent
agent -> alib
agent -> http: "--source-address"
reader -> alib
reader -> local
```

| Target | Depends on | Purpose |
| --- | --- | --- |
| `detent` | the core libraries below | Client, schema, evaluation, overrides, file source |
| `detent_http` | `detent`, `compressors` | Plain POSIX-socket HTTP poller; no Asio |
| `detent_agent_lib` | `detent` | Shared-memory agent logic, linkable by tests |
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
| [http](https://github.com/Kronuz/http) + [reactor](https://github.com/Kronuz/reactor), [http-parser](https://github.com/Kronuz/http-parser), [radix-router](https://github.com/Kronuz/radix-router) | `detentd`'s HTTP server |
| [consensus](https://github.com/Kronuz/consensus) | Durable Raft for the authority, with journal and completion I/O |
| [logger](https://github.com/Kronuz/logger) | Daemon logging |
| [Asio](https://think-async.com/Asio/) | Networking, pulled in by `http` and `consensus` |
| [OpenSSL](https://www.openssl.org/) 3.0+ | TLS for the authority and managed delivery |

RapidJSON is pinned to commit `24b5e7a8`, because the 1.1.0 release doesn't
compile with Clang 23. The logger is pinned to a specific commit as well; most
other Kronuz libraries currently follow their default branch.

:::hint[Pinned revisions]{.unimplemented}
Most Kronuz dependencies are fetched at `main`, so a build can change when an
unrelated library changes. Pinning a tested revision set, and publishing it with
each Detent release, is on the [Roadmap](/Detent/roadmap/#build-and-release).
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
