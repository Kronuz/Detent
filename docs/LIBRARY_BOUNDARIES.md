**Library boundaries: what Detent added to the Kronuz libraries, and what should move**

**Author:** [Germán Méndez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-10-10
**Status:** Recommendation, for the implementing agent

## Abstract

Detent's durable-authority work made the dependency libraries reusable on purpose, which was the right instruction. The result went too far in one direction: almost all of it landed inside `Kronuz/cluster`, which grew from about **900 lines** (a UDP multicast bus, a legacy Raft and a length codec) to about **12,400 lines** plus **6,800 lines of tests**. Three independent subsystems now live there: `completion` (asynchronous file I/O), `journal` (durable record storage) and `consensus` (strict durable Raft). None of them uses the original bus or Raft, and Xapiand, the other consumer of `cluster`, uses none of them.

At the same time, Detent keeps several pieces that are generic but private: a Raft peer transport, three separate HTTP clients, five variations of a resource ledger, a persistent tree and a narrow MessagePack codec.

The recommendation:

1. Split `cluster` into `completion`, `journal` and `consensus` repositories, and return `cluster` to the bus and legacy Raft that Xapiand uses.
2. Move Detent's authority peer transport into `consensus`.
3. Extract a small `metrics` library and a single `ledger` primitive.
4. Give `Kronuz/http` the client half and share one TLS layer.
5. Extract the persistent map and the bounded MessagePack codec.

Everything knob-specific stays in Detent. The splits are mechanical: each target already compiles as its own CMake target with no reverse dependencies.

## Background

`Kronuz/cluster` is the networking substrate extracted from Xapiand: `cluster::Bus` (UDP multicast messaging) and `cluster::Raft` (leader election and a replicated log over that bus). Xapiand fetches it at `main` and includes `bus.h`, `raft.h` and `length.h`.

Detent's durable authority needed things the legacy Raft never provided: persisted terms and votes, a crash-safe log, checkpoints and completion-based disk I/O. Those were built as "generic, reusable" modules, which is correct. Where they were put is the problem this document addresses.

## Findings

### Library change inventory

Commits since 2026-09-25 (the Detent work), against each library's prior state.

| Library | Commits | Change | Verdict |
| --- | ---: | --- | --- |
| `cluster` | 69 | +3 subsystems, ~11.5k lines plus tests | **Split** (below) |
| `http` | 8 | Owned deferred replies, TLS sessions, admission and deadlines; +2,447 lines | Right home |
| `reactor` | 5 | Connection admission, listener readiness, session reaping; +473 lines | Right home |
| `cpp-btree` | 5 | Heterogeneous lookup, `construct_at`, diagnostics | Right home |
| `sha256` | 2 | Padding pointer and alignment fixes | Right home |
| `varint` | 1 | Overflow rejection ported from `cluster` | Right home |

The `http`, `reactor`, `cpp-btree`, `sha256` and `varint` changes are generic improvements to the thing each library already is. They stay.

### `cluster` is four libraries in one repository

| Directory | Lines (code + docs) | Depends on | Used by |
| --- | ---: | --- | --- |
| root: `bus.h`, `raft.h`, `length.h` | ~970 | `reactor`, `varint` | Xapiand, Detent's legacy `detentd` |
| `completion/` | 4,389 | Threads (Asio optional) | `journal`, Detent `detent_local` |
| `journal/` | 3,361 | `completion` | `consensus`, Detent authority |
| `consensus/` | 3,738 | `journal` | Detent authority |

The include graph is a clean stack with exactly one link back to the original code:

```text
completion  <-  journal  <-  consensus          no Asio, no reactor, no bus or raft
bus.h, raft.h, length.h  ->  reactor, varint
root metrics.h  ->  completion/metrics.h        the only link between the two halves
```

The modules already behave like separate libraries. `completion` declares `kronuz::completion` and `kronuz::completion_asio`, and `journal/CMakeLists.txt` says it "can be built without fetching Asio or reactor". Each has its own `README.md`, benchmarks and test suite.

Living in one repository has concrete costs:

1. **Xapiand inherits them.** The root `CMakeLists.txt` adds `journal` and `consensus` unconditionally, so every Xapiand configure pulls in about 11k lines it never includes, and every `cluster` push at `main` can break Xapiand's build for reasons unrelated to Xapiand.
2. **Two unrelated Raft implementations share a name.** `cluster::Raft` (volatile, multicast) and `cluster::consensus` (durable, point-to-point) are easy to confuse, and SAFETY and ARCHITECTURE documents have to explain both.
3. **Hidden dependency.** `consensus/` includes `../journal/*.h`, but the `cluster_consensus` target never links `cluster::journal`. It works only because of the shared include root.
4. **Compatibility debris.** `journal/` keeps 4 to 9-line forwarding headers (`asio_completion.h`, `posix.h`, `io.h` and others) from when `completion` was extracted, and `completion/image.h` carries parallel "legacy" and "managed" variants of the same API. Detent is the only consumer of either, so nothing needs the legacy paths.
5. **Library READMEs are qualification logs.** `completion/README.md`, `journal/README.md` and `consensus/README.md` record sanitizer runs and "Detent publication suites pass" results. A library README should say what the library does, its API and its guarantees; the evidence belongs in Detent's `docs/` or a changelog.

### Generic code that lives inside Detent

These files contain no knob concepts and depend only on library types.

| Component | Location | Lines | What it is |
| --- | --- | ---: | --- |
| Authority peer transport | `src/detent_authority/transport/` | 1,267 | Raft over TLS: peer hello, `DTC1` codec, control and bulk lanes, snapshot writes, RPC timing. Uses only `consensus::` and `journal::` types |
| HTTP clients (three) | `detent_http/http_client.*`, `detent_delivery/client.*` + `response.*`, `transport/https_client.hh` | ~1,770 | A blocking keep-alive poller, an Asio coroutine TLS streaming client, and a TLS status probe |
| TLS plumbing | `detent_transport/tls.hh`, `transport/tls.hh`, `transport/session.hh` | 382 | Context setup, chain and pin verification, deadline-bound sessions. `http` has its own `http_asio_tls.h` |
| Persistent map | `src/detent_authority/persistent_map.hh` | 215 | Immutable AVL tree with path copying and budgeted collection; no includes |
| Bounded MessagePack | `include/detent_distribution/messagepack.hh` | 312 | Narrow, allocation-bounded tuple encoder and decoder; no includes |
| Ledgers and retirement | `detent_distribution/{storage,allocator,buffer,memory_resource,retirement}.hh` + `retirement.cc` | ~980 | Concurrent byte ledger with tickets, PMR adapter, owned buffers, budgeted retirement runtime |
| Disk ledger | `include/detent_local/disk_ledger.hh` | 169 | Single-owner logical disk accounting with explicit settlement |
| Daemon plumbing | `src/detent_cli/daemon.cc`, `config.cc` | 293 | Daemonize, PID lock, readiness reporting, platform config/state/runtime directories |

### One idea, implemented five times

Bounded resource accounting with move-only permits shows up in five places with different names:

| Implementation | Location | Model |
| --- | --- | --- |
| `Admission`, `StoragePermit` | `cluster/journal/admission.h` | Single executor, storage bytes, explicit fencing |
| `AllocationContext`, `OwnedAllocator` | `cluster/completion/allocation.h` | PMR context retained by every owner |
| `StorageLedger`, `Ticket`, `PagedStorage` | Detent `detent_distribution/storage.hh` | Concurrent, follows the allocation |
| `DiskLedger`, `Permit` | Detent `detent_local/disk_ledger.hh` | Single owner, logical disk, no refund on destruction |
| Connection and request admission | `reactor`, `http`, Detent `subscription_admission` | Leases held through original I/O |

The semantics differ in ways that matter (concurrent versus single-owner, refund on destruction versus explicit settlement), so a single class cannot replace all of them. They still share a core: a capacity, a move-only permit, settlement and a pressure signal. Each project currently re-derives that core.

`kronuz::metrics` (counters and duration histograms) has the same shape of problem: it lives in `completion/metrics.h`, `cluster/metrics.h` forwards to it, and Detent's authority (`node.hh`, `service.cc`, the peer transport) uses it through `completion`, a storage library it otherwise has no reason to name for metrics.

## Recommendation

### Proposed library map

| Library | Contents | Depends on |
| --- | --- | --- |
| `Kronuz/metrics` (new) | `Counter`, `AtomicCounter`, `DurationHistogram` from `completion/metrics.h` | none |
| `Kronuz/ledger` (new) | Capacity ledger and permits (single-owner and concurrent flavors), PMR adapter, budgeted retirement | none |
| `Kronuz/completion` (new, from `cluster/completion`) | Completion I/O backends, image publication, directory scan | `metrics`, `ledger` |
| `Kronuz/journal` (new, from `cluster/journal`) | Durable journal, artifacts, mutations, reclamation, inventory | `completion` |
| `Kronuz/consensus` (new, from `cluster/consensus`) | Strict durable Raft `Core` and `Worker`, checkpoints, snapshot channel; `transport/` with Detent's peer transport | `journal`; `reactor` for `transport/` only |
| `Kronuz/cluster` | `bus.h`, `raft.h`, `length.h`, liveness | `reactor`, `varint` |
| `Kronuz/http` | Server as today, plus a client (blocking keep-alive and Asio coroutine) | `reactor` |
| `Kronuz/reactor` | Adds the shared TLS context and session layer used by `http` and `consensus/transport` | Asio, OpenSSL (optional) |
| `Kronuz/persistent-map` (new) | The immutable AVL tree | none |
| `Kronuz/msgpack` | Adds the bounded tuple encoder and decoder as a standalone header | none |

Two smaller candidates stay in Detent until a second consumer shows up:

1. **Daemon plumbing.** It duplicates what Xapiand does at startup. Extract it when Xapiand adopts it, not before.
2. **Framing.** The bounded raw/zstd frame format belongs to Detent's distribution protocol. Its generic part (bounded decompression) belongs in `compressors`.

### What stays in Detent

The knob model and evaluation, the JSON schema and patches, the authority `State` and command codec, `node.cc` (it binds `consensus` to Detent's `State`), the distribution schema (cursor, profile, rows), the local image format, the agent, and the delivery coordinator's domain logic. These are Detent's reason to exist.

### Alternatives considered

1. **Leave everything in `cluster`.** It costs nothing now, but Xapiand keeps absorbing churn it doesn't use, and the repository name stops describing its contents.
2. **Fold everything into one `kronuz-storage` monorepo.** That reduces repository count, but it recreates the same coupling one level down: a consumer of `completion` would fetch consensus.
3. **Merge `ledger` into the existing `Kronuz/allocators`.** This is reasonable, since `allocators` already accounts every byte. It's not chosen because `allocators` overrides global `new`/`delete` and is process-wide, while the ledger is per-owner and per-component. A consumer should be able to take one without the other.

## Implementation plan

Each step lands and qualifies independently, in this order, so every intermediate state builds from public sources.

1. **`metrics`.** Move `completion/metrics.h` to `Kronuz/metrics`. `completion` and Detent link `metrics::metrics`, and `reactor` and `http` can adopt it for their admission observers. Delete `cluster/metrics.h`.
2. **`completion`, `journal`, `consensus`.** Create each repository with `git filter-repo --subdirectory-filter` so history is preserved. Do not rewrite `cluster` history: Detent's docs cite `cluster` commit IDs, and those must keep resolving. Remove the moved directories from `cluster` in one ordinary commit that points to the new homes. In the same step, add the missing `cluster::journal` link to the consensus target, delete the `journal/` forwarding headers, and drop the unconditional `add_subdirectory` calls from `cluster`.
3. **Consensus transport.** Move `src/detent_authority/transport/` (except `https_client.hh`) into `consensus/transport/` as an optional target that needs `reactor` and OpenSSL. Rename `detent::transport` to `kronuz::consensus::transport`. The `DTHL0001` and `DTC1` wire tags stay byte-identical; rename them only with a versioned format change.
4. **Remove legacy APIs.** Drop the "legacy" halves of `completion/image.h` and similar dual paths, since Detent is the only consumer.
5. **`ledger`.** Extract the shared core: a capacity, a move-only permit, explicit settlement, pressure, and the retirement runtime. Keep `journal::Admission`, `DiskLedger` and `StorageLedger` as thin domain wrappers over it. Behavior and tests must not change.
6. **HTTP client and TLS.** Add the client half to `http` with two front ends: a blocking keep-alive client (today's `detent_http`) and an Asio coroutine client with absolute deadlines and streaming bodies (today's `detent_delivery` transport). Move TLS context and session handling into `reactor`, and have `http_asio_tls.h` and `consensus/transport` use it. Detent keeps only request and response semantics.
7. **`persistent-map` and `msgpack`.** Move the files with their tests; Detent includes them as dependencies.
8. **Pin revisions.** Every `FetchContent_Declare` in Detent, `cluster` and the new libraries uses a tested commit instead of `main`. This is already open in `docs/DOCUMENTATION_FINDINGS.md`.
9. **Rewrite the library READMEs.** Each one says what the library is, shows a minimal example, and states its guarantees and limits. Qualification evidence moves to Detent's `docs/` (or a `CHANGELOG`), and no library README names Detent.

### Acceptance gates for every step

1. The new or changed library's own suite passes natively on macOS, Linux and FreeBSD, and under the sanitizers that qualified the original code.
2. Detent's full suite passes, **74 tests** today, from a clean clone with no `FETCHCONTENT_SOURCE_DIR_*` overrides.
3. Xapiand builds against the new `cluster`, and its cluster harness passes with unchanged wire bytes.
4. On-disk formats (journal, checkpoint, local image, descriptor) and wire formats are byte-identical before and after. Each move is a relocation, not a redesign.
5. The Homebrew formula (`packaging/homebrew/detent.rb`) still builds and passes `brew test` at the new pinned revision.

## Next steps

Start with steps 1 and 2. They remove the largest source of clutter (about 11.5k lines out of `cluster`) at the lowest risk, because the stack already compiles as separate targets. Steps 3 through 7 can follow in any order once the new repositories exist. Report the before-and-after line counts per repository in this document when each step lands, and update the Roadmap and Dependencies pages in `website/` to match.
