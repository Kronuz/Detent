---
title: "Installation"
---

Detent is built from source with CMake. There are no binary packages yet.

---

## Requirements

1. **A C++20 compiler and standard library** that provide `std::jthread` and
   stop tokens. Configuration checks this before building. libc++ 20 and newer,
   recent libstdc++, and Apple Clang 17 all work.
2. **CMake 3.24** or newer.
3. **Git and network access** on the first configure; every dependency is
   fetched with CMake `FetchContent`.
4. **OpenSSL 3.0** or newer for the managed agent and the durable authority.
5. For the integration tests only: Python 3, Bash, curl, and the `openssl`
   command-line tool on `PATH`. The running daemons don't need them.

Detent is developed and qualified on **macOS**, **Linux** and **FreeBSD**.

:::hint[FreeBSD with base libc++ 19]{.caution}
libc++ 19 hides `std::jthread` behind its experimental library. Configure with
`-DCMAKE_CXX_FLAGS=-fexperimental-library`, and build every consumer of the
public C++ headers with the same flag. This opts into libc++'s experimental
API/ABI; prefer a stable libc++ 20 when it's available. Bash isn't part of the
FreeBSD base system, so install it before running the tests.
:::

---

## Building

```sh
git clone https://github.com/Kronuz/Detent.git
cd Detent
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

That produces, under `build/src/`:

| Binary | What it is |
| --- | --- |
| `detentd` | The HTTP distributor, optionally clustered, or the durable authority |
| `detent-agent` | The host-local agent daemon |
| `detent` | The shell reader: `detent get NAME` |

Authority builds add `build/src/detent_authority/detent-authority-admin`, the
import/export tool. You also get the static libraries, examples in `build/examples/`, benchmarks in
`build/benchmarks/`, and the test suite.

:::hint[Unpublished dependency revisions]{.warning}
Some of Detent's sibling libraries (`cluster`, `http`, `reactor`, `sha256` and
`cpp-btree`) currently carry local changes that Detent relies on and that
haven't been pushed to their public repositories yet. Until they are, a clean
clone that fetches those libraries from GitHub may fail to configure or build.
Point CMake at local checkouts with `FETCHCONTENT_SOURCE_DIR_<NAME>`, for
example:

```sh
cmake -B build \
  -DFETCHCONTENT_SOURCE_DIR_CLUSTER=$PWD/../Kronuz/cluster \
  -DFETCHCONTENT_SOURCE_DIR_HTTP_LIB=$PWD/../Kronuz/http \
  -DFETCHCONTENT_SOURCE_DIR_REACTOR=$PWD/../Kronuz/reactor \
  -DFETCHCONTENT_SOURCE_DIR_SHA256=$PWD/../Kronuz/sha256 \
  -DFETCHCONTENT_SOURCE_DIR_CPP_BTREE=$PWD/../Kronuz/cpp-btree
```

The [Roadmap](/Detent/roadmap/#build-and-release) tracks publishing them.
:::

### Build Options

| Option | Default | Effect |
| --- | --- | --- |
| `DETENT_BUILD_DISTRIBUTOR` | `ON` | Build `detentd` and its HTTP/reactor/cluster dependency chain |
| `DETENT_BUILD_MANAGED_AGENT` | `ON` | Build managed (authority) delivery support into `detent-agent` |
| `DETENT_BUILD_AUTHORITY` | `OFF` | Build the opt-in durable authority profile of `detentd` |
| `DETENT_BUILD_TESTS` | `ON` | Build the test suite |
| `DETENT_BUILD_EXAMPLES` | `ON` | Build `detent_demo` and `detent_publish` |
| `DETENT_BUILD_BENCHMARKS` | `ON` | Build the benchmarks |
| `DETENT_BUILD_PYTHON` | `OFF` | Build the pybind11 Python module (needs Python 3 headers) |
| `DETENT_BUILD_RUST` | `OFF` | Build the `cxx` Rust crate (needs `cargo` and `rustc`) |

A library-only build, for an application that only needs `detent::Client` with
baked defaults or a file source, can skip the network stack entirely:

```sh
cmake -B build -DDETENT_BUILD_DISTRIBUTOR=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF
```

---

## Testing

```sh
ctest --test-dir build --output-on-failure
```

The suite covers the core client (schema validation, golden hash vectors,
evaluation precedence, CLI overrides, the file source, concurrency stress),
the distributor (PUT/GET/PATCH, persistence, diffs, multi-node polling,
fan-out load, a real three-node Raft cluster), the agent's shared-memory
reader and writer, and the authority, delivery and local-image libraries.
A separate harness in `tests/e2e/` spawns real `detentd` processes and kills
them with real signals; run it after changing the distributor or its Raft
wiring.

---

## Installing

```sh
cmake --install build --prefix /usr/local
```

This installs `detentd`, `detent-agent` and `detent` into `bin/`, and the
service templates for systemd, launchd and rc.d into `share/detent/`. See
[System Services](/Detent/operations/services/) to run them under a service
manager.

---

## Using Detent from Your Own CMake Project

```cmake
include(FetchContent)
FetchContent_Declare(detent
  GIT_REPOSITORY https://github.com/Kronuz/Detent.git
  GIT_TAG        main)
FetchContent_MakeAvailable(detent)

target_link_libraries(my_app PRIVATE detent::detent)
# Only if you want the HTTP poll source:
target_link_libraries(my_app PRIVATE detent_http::detent_http)
```

`add_subdirectory()` of a local checkout works the same way. See
[Using Detent in a C++ Application](/Detent/guides/cpp-application/) for the
rest.

:::hint[Pin a revision]{.tip}
Detent has no tagged releases yet. For reproducible builds, set `GIT_TAG` to a
specific commit rather than `main`.
:::
