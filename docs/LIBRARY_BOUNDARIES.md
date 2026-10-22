**Library boundaries: what Detent added to the Kronuz libraries, and what should move**

**Author:** [Germán Méndez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-10-10
**Status:** Recommendation, for the implementing agent. Steps 1 to 3, and the TLS part of step 6, landed on 2026-10-13 (see [Progress](#progress)).

## Abstract

Detent's durable-authority work made the dependency libraries reusable on purpose, which was the right instruction. The result went too far in one direction: almost all of it landed inside `Kronuz/cluster`, which grew from about **900 lines** (a UDP multicast bus, a legacy Raft and a length codec) to about **12,400 lines** plus **6,800 lines of tests**. Three independent subsystems now live there: `completion` (asynchronous file I/O), `journal` (durable record storage) and `consensus` (strict durable Raft). None of them uses the original bus or Raft, and Xapiand, the other consumer of `cluster`, uses none of them.

At the same time, Detent keeps several pieces that are generic but private: a Raft peer transport, three separate HTTP clients, five variations of a resource ledger, a persistent tree and a narrow MessagePack codec.

The recommendation:

1. Split `cluster` into `completion`, `journal` and `consensus` repositories, leaving `cluster` with the bus and the legacy Raft. Then measure the legacy Raft against `consensus`, and retire it unless the numbers show it earns its place.
2. Move Detent's authority peer transport into `consensus`.
3. Extract a small `metrics` library and a single `ledger` primitive.
4. Give `Kronuz/http` the client half and share one TLS layer.
5. Extract the persistent map and the bounded MessagePack codec.

Everything knob-specific stays in Detent. The splits are mechanical: each target already compiles as its own CMake target with no reverse dependencies.

## Ground rules

1. **Detent is greenfield.** Nothing in it, or in the libraries it pulls in, is kept for backward compatibility with itself. Design for the best end state, not the smallest diff.
2. **Xapiand compatibility is the only constraint, and it is flexible.** Shared libraries must stay usable by Xapiand, but when a library change calls for it, refactor Xapiand in the same series instead of bending the library around it.
3. **Nothing is kept just because Xapiand uses it.** Every component earns its place with a concrete reason (a capability, a measured advantage), whoever consumes it.

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

### Who depends on what after the split

```text
Detent ──> consensus ──> journal ──> completion        durable authority (--authority)
   └─────> cluster ──> reactor, varint                 classic clustering (--cluster)

consensus does not depend on cluster; Xapiand depends only on cluster
```

Detent keeps both dependencies. `src/detentd/` (`detentd.cc`, `raft_delegate.*`, `main.cc`) uses `cluster::Bus` and `cluster::Raft` for classic `--cluster` mode, and `src/detent_authority/` uses consensus and journal. If the authority later replaces classic clustering, Detent drops `cluster` entirely. That retirement is a separate product decision, not part of this plan.

Nothing in `consensus`, `journal` or `completion` includes `bus.h`, `raft.h` or `length.h`. The only tie is the namespace: the durable code lives in `cluster::consensus` while journal already uses `kronuz::journal`. The split renames it to `kronuz::consensus` (about 90 references in Detent).

### Legacy Raft: measure, then keep or retire

After the split there are two Raft implementations: `cluster::Raft` (volatile terms and votes, UDP multicast through `cluster::Bus`, membership from configured peers and observed liveness) and `consensus` (persisted terms, votes and log, a pure deterministic `Core`, fixed 3 or 5 voters, authenticated point-to-point transport). Keeping both is justified only if the legacy one delivers something `consensus` doesn't. Measure before deciding.

Run both on 3 and 5 nodes, on loopback and on a real network:

| Measurement | Why it matters |
| --- | --- |
| Time to first leader from a cold start | Cluster bootstrap |
| Failover time after the leader is killed | Availability |
| Restart-to-rejoin time | The legacy Raft sits out `3 × election_max` (90 s at defaults) after a restart |
| Commit latency, p50 and p99, at Xapiand's real write rate and at 10× | Write path |
| CPU per committed entry, idle CPU, RSS | Cost; convert to cores and dollars at fleet scale |
| Network bytes per committed entry, idle traffic | Heartbeat and replication overhead |
| Binary size and compile time added | Footprint |
| Behavior under partition, duplicate and reordered messages, and kill/restart | Safety: volatile votes are a known weakness of the legacy Raft |

Run `consensus` twice: once with durable `journal` storage, and once with the cheapest storage mode it supports. That separates the algorithm's cost from the cost of durability, which the legacy Raft never pays.

Two functional gaps decide the outcome as much as the numbers:

1. **Discovery and dynamic membership.** Xapiand forms clusters from multicast discovery, while `consensus` has fixed voters and no membership changes. Retiring the legacy Raft means either adding membership changes to `consensus` or running Xapiand with configured voters. `cluster::Bus` can stay as the discovery layer either way: retiring the Raft does not retire the bus.
2. **Transport.** `consensus` has no multicast transport. The peer transport moved in step 3 (TLS over TCP) would be what Xapiand uses.

**Decision rule.** Keep `cluster::Raft` only if the legacy Raft is meaningfully cheaper at Xapiand's real workload (enough that a deployment would notice in cores, latency or bytes), or if it provides a capability `consensus` can't reasonably add. Otherwise retire it: move Xapiand to `consensus` (with `cluster::Bus` for discovery if that's still useful), refactor Xapiand accordingly, and delete `raft.h`. Detent's classic `--cluster` mode then either moves to `consensus` or is retired in favor of the authority. Record the measurements and the decision in this document.

### What stays in Detent

The knob model and evaluation, the JSON schema and patches, the authority `State` and command codec, `node.cc` (it binds `consensus` to Detent's `State`), the distribution schema (cursor, profile, rows), the local image format, the agent, and the delivery coordinator's domain logic. These are Detent's reason to exist.

### Alternatives considered

1. **Leave everything in `cluster`.** It costs nothing now, but Xapiand keeps absorbing churn it doesn't use, and the repository name stops describing its contents.
2. **Fold everything into one `kronuz-storage` monorepo.** That reduces repository count, but it recreates the same coupling one level down: a consumer of `completion` would fetch consensus.
3. **Merge `ledger` into the existing `Kronuz/allocators`.** This is reasonable, since `allocators` already accounts every byte. It's not chosen because `allocators` overrides global `new`/`delete` and is process-wide, while the ledger is per-owner and per-component. A consumer should be able to take one without the other.

## Implementation plan

Each step lands and qualifies independently, in this order, so every intermediate state builds from public sources.

1. **`metrics`.** Move `completion/metrics.h` to `Kronuz/metrics`. `completion` and Detent link `metrics::metrics`, and `reactor` and `http` can adopt it for their admission observers. Delete `cluster/metrics.h`.
2. **`completion`, `journal`, `consensus`.** Create each repository with `git filter-repo --subdirectory-filter` so history is preserved. Do not rewrite `cluster` history: Detent's docs cite `cluster` commit IDs, and those must keep resolving. Remove the moved directories from `cluster` in one ordinary commit that points to the new homes. In the same step, rename the `cluster::consensus` namespace to `kronuz::consensus`, add the missing `cluster::journal` link to the consensus target, delete the `journal/` forwarding headers, and drop the unconditional `add_subdirectory` calls from `cluster`.
3. **Consensus transport.** Move `src/detent_authority/transport/` (except `https_client.hh`) into `consensus/transport/` as an optional target that needs `reactor` and OpenSSL. Rename `detent::transport` to `kronuz::consensus::transport`. The `DTHL0001` and `DTC1` wire tags stay byte-identical; rename them only with a versioned format change.
4. **Remove legacy APIs.** Drop the "legacy" halves of `completion/image.h` and similar dual paths, since Detent is the only consumer.
5. **`ledger`.** Extract the shared core: a capacity, a move-only permit, explicit settlement, pressure, and the retirement runtime. Keep `journal::Admission`, `DiskLedger` and `StorageLedger` as thin domain wrappers over it. Behavior and tests must not change.
6. **HTTP client and TLS.** Add the client half to `http` with two front ends: a blocking keep-alive client (today's `detent_http`) and an Asio coroutine client with absolute deadlines and streaming bodies (today's `detent_delivery` transport). Move TLS context and session handling into `reactor`, and have `http_asio_tls.h` and `consensus/transport` use it. Detent keeps only request and response semantics.
7. **`persistent-map` and `msgpack`.** Move the files with their tests; Detent includes them as dependencies.
8. **Pin revisions.** Every `FetchContent_Declare` in Detent, `cluster` and the new libraries uses a tested commit instead of `main`. This is already open in `docs/DOCUMENTATION_FINDINGS.md`.
9. **Rewrite the library READMEs.** Each one says what the library is, shows a minimal example, and states its guarantees and limits. Qualification evidence moves to Detent's `docs/` (or a `CHANGELOG`), and no library README names Detent.
10. **Legacy Raft decision.** Run the comparison in [Legacy Raft: measure, then keep or retire](#legacy-raft-measure-then-keep-or-retire) once step 2 has landed, and act on the result. This can run in parallel with steps 3 through 9.

### Acceptance gates for every step

1. The new or changed library's own suite passes natively on macOS, Linux and FreeBSD, and under the sanitizers that qualified the original code.
2. Detent's full suite passes, **74 tests** today, from a clean clone with no `FETCHCONTENT_SOURCE_DIR_*` overrides.
3. Xapiand builds against the changed libraries and its cluster harness passes. Its wire format may change when a library change calls for it; in that case, refactor Xapiand in the same series.
4. Relocation steps (1 through 3, 5 through 7) keep on-disk and wire formats byte-identical, so a move never hides a behavior change. Format changes are allowed, since Detent is greenfield, but they land as their own versioned commits.
5. The Homebrew formula (`packaging/homebrew/detent.rb`) still builds and passes `brew test` at the new pinned revision.

## Next steps

Start with steps 1 and 2. They remove the largest source of clutter (about 11.5k lines out of `cluster`) at the lowest risk, because the stack already compiles as separate targets. Steps 3 through 7 can follow in any order once the new repositories exist, and the legacy Raft comparison (step 10) can start as soon as step 2 lands. Report the before-and-after line counts per repository in this document when each step lands, and update the Roadmap and Dependencies pages in `website/` to match.

## Progress

### Historical retirement correction (2026-10-18)

Local consensus `3c60f47` preserves progress after operating limits decrease: one first oversized historical command or configuration may consume a fresh nonzero retirement budget, with remaining bytes saturated to zero. Output cleanup and retained-log cleanup share the same budget object, preventing both a renewed allowance and unsigned subtraction underflow. Zero-byte predecessors and explicit zero budgets retain bounded behavior. Core, retained-log, retirement and compatibility suites pass in an isolated build (4/4); a published-revision reproducer changes from three entries retained after 1,000 turns to complete drainage in three. The broader resource preparation also passes its 25 Worker checks. No public wire/disk bytes change, and Detent's pin remains unchanged pending finalized-v2 qualification. The cpp-libraries catalog is updated in local `63d9288`.

### Current source boundaries and reuse audit (2026-10-18)

The source directories separate application roles and optional dependency closures. They are not proposed standalone repositories. This source-only pass checks the current cpp-libraries catalog, its Rules of engagement and the owner's GitHub repository inventory; extraction and replacement still require API, lifetime, format and performance qualification.

| Directory | Responsibility | Boundary to retain |
| --- | --- | --- |
| `detent_agent` | Host-local daemon, classic shared-memory publication and managed delivery host | Agent policy and process assembly |
| `detent_authority` | Knob transactions, replicated state, receipts, scoped exports and subscriptions | Application state and the adapter to shared consensus |
| `detent_cli` | Platform paths, daemon lifecycle, PID locks and logging setup | Application configuration; evaluate generic process plumbing separately |
| `detent_delivery` | Receiving updates, source selection, authenticated sessions and local-publication coordination | Detent delivery semantics; separate generic HTTP client mechanics |
| `detent_distribution` | Snapshot/delta schemas, cursors, encoding, framing and subscription admission | Detent protocol; extract generic codecs and resource primitives |
| `detent_distribution_http` | HTTP binding shared by authority and relay, including reply pumping and session headers | Protocol-specific HTTP metadata; evaluate generic pumping separately |
| `detent_http` | Optional blocking HTTP polling source for the classic SDK/agent path | Source adapter; consolidate generic client mechanics into `http` |
| `detent_local` | Flat/paged local images, readers, validation, atomic publication, leases and collection | Knob layouts and ownership rules; reuse lower-level I/O and residency where their contracts fit |
| `detent_relay` | Scoped fanout from upstream delivery through durable local bundles | Relay authorization, bundle routing and serving policy |

The existing extraction has already moved completion I/O, journals, consensus, metrics, peer transport and TLS out of Detent. Their integration adapters remain intentionally local. Current `http` provides server machinery and shared parsing but no equivalent general client API for Detent's three paths: blocking polling, coroutine delivery and HTTPS health checks. Extend `http` with shared client mechanics, keeping Detent schemas, tokens, cursors and session decisions in its adapters.

The bounded codec in `include/detent_distribution/messagepack.hh` is an extraction candidate into `msgpack`, with one bounded low-level implementation and a small optional target. The existing general-purpose DOM is not a proven replacement for its incremental decoding and allocation bounds. `persistent_map.hh` is another candidate: immutable snapshots and budgeted reclamation differ from the mutable `cpp-btree` API. No dedicated ledger or persistent-map repository appears in the checked GitHub inventory.

Resource accounting overlaps across Detent, completion and journal. Share primitives only after separating concurrent allocation tickets from disk permits that require explicit durable settlement; replacing the latter with automatic destructor refunds would change correctness. Evaluate `allocators` before creating a ledger library. Evaluate `task`, `residency` and `flume` for scheduling, mapped reads and generic transfer, but none is established here as a replacement for Detent's state machines, generation leases or snapshot/delta protocol. In particular, residency's synchronous prefetch backend is advisory and does not guarantee fault-free event-loop reads.

Prioritize client consolidation and bounded MessagePack reuse, then the persistent map and accounting primitives. Keep extraction commits separate from dependency-only comparisons and preserve valid wire/disk bytes. No extraction, dependency pin change or runtime qualification occurs in this audit.

### Steps 1 and 2 (2026-10-13)

`metrics`, `completion`, `journal`, and `consensus` are their own repositories, extracted from `cluster` with `git filter-repo` so their history comes along; `cluster`'s own history is not rewritten, and its removal is one ordinary commit (`e70b5fc`), so commit IDs cited in these docs keep resolving.

| Repository | Lines (headers and sources) | Contents |
| --- | ---: | --- |
| `cluster` before | 20,918 | Bus, legacy Raft, length codec, liveness, and the four below |
| `cluster` after | 2,502 | Bus, legacy Raft, length codec, liveness |
| `metrics` | 254 | `metrics.hh`, its test and benchmark |
| `completion` | 4,619 | `include/completion/`, its tests |
| `journal` | 5,943 | `include/journal/`, tests, benchmarks, demo |
| `consensus` | 7,359 | `include/consensus/`, tests, benchmarks, demo |

Decisions made while splitting:

1. **Names.** CMake targets follow the catalog's `<library>::<library>` convention: `metrics::metrics`, `completion::completion` (and `completion::asio`), `journal::journal`, `consensus::consensus`; `cluster::journal`, `cluster::journal_completion`, and `kronuz::completion` are gone. C++ namespaces are `kronuz::metrics`, `kronuz::journal` (the I/O types in `completion` keep it), and `kronuz::consensus` (renamed from `cluster::consensus`).
2. **Layout.** Headers live in `include/<library>/`, so every `#include "completion/..."`, `"journal/..."`, and `"consensus/..."` line is unchanged. `metrics.h` became `metrics.hh` at the root, like the other single-header libraries.
3. **Dependencies are explicit and pinned.** `completion` fetches `metrics`, `journal` fetches `completion`, `consensus` fetches `journal`, each at a tested commit. `consensus` links `journal` (the hidden dependency in Findings item 3). `reactor` is fetched only for `consensus`'s Asio completion test and benchmark.
4. **Compatibility debris removed.** `journal/`'s eight forwarding headers are deleted; callers include `completion/` headers directly (Detent's `<journal/posix.h>`, `<journal/native_completion.h>`, and `<journal/asio_completion.h>` lines changed accordingly). The test that needed `consensus` (`completion_asio_test`) moved to `consensus`.
5. **READMEs.** Commands, names, and paths are corrected; the rewrite of qualification logs into library documentation remains step 9.

Detent: the top-level `CMakeLists.txt` fetches `cluster` only for the distributor's classic mode, `consensus` for the authority, and `completion` otherwise, each new one pinned. The authority links `consensus::consensus` instead of reaching `consensus/` through `cluster`'s include root; `terminal_retirement_bench` links `reactor::reactor`, since it uses Asio directly; about 90 references move from `cluster::consensus` to `kronuz::consensus`.

Acceptance:

- Each new library's suite passes on macOS: `metrics` 1 test, `completion` 11 (with the Asio adapter), `journal` 4, `consensus` 6 (with the Asio completion test); `cluster` keeps 4 (bus, Raft, Raft safety, liveness).
- Detent, all options on (authority, distributor, managed agent, relay, benchmarks, examples, tests), compared with the same build before the split: 180 of 181 tests build and pass in both. The exception, `test_local_paged_compaction`, did not compile before the split either (uncommitted work in progress calls a missing `DiskStore::collect`). Under parallel ctest a different pair of timing-sensitive tests fails in each run, before the split and after; each passes alone, and the serial run passes.
- Xapiand builds against the new `cluster`. Removing `cluster/metrics.h` also removes a header that shared a name with Xapiand's own `src/metrics.h` on the same include path.
- strata builds and passes against `completion` directly.
- Linux (the strata gate VM, Ubuntu 24.04, clang 18): `metrics` 1, `completion` 10, `journal` 5 (with the io_uring backend test), `consensus` 6, and `cluster` 4 tests pass.
- A clean clone of Detent at `9621536`, with no `FETCHCONTENT_SOURCE_DIR_*` overrides and every option on, builds and passes all 180 tests run serially (gate 2).
- Not yet run: FreeBSD (gate 1) and the Homebrew formula (gate 5).

Next: step 3 (the authority peer transport into `consensus/transport/`), then steps 4 through 10.

### Step 3, and the TLS part of step 6 (2026-10-13)

The authority peer transport lives in `consensus` as the optional target `consensus::transport` (`include/consensus/transport/`, namespace `kronuz::consensus::transport`): codec, hello, peers, RPC observation, session, snapshot writes, TLS pinning, and the profile fingerprint. The `DTHL0001` hello, the `DTC1` codec, and the `DTPROF01` fingerprint are byte-identical.

- **Decoupled from Detent.** `Peers` was built on Detent's `authority::Node` and `NodeOptions`; it is now `Peers<Host>` for any host with `worker()` and `channel()` (the `PeerHost` concept), configured by a `PeerConfiguration` (voters, consensus limits, cluster, group, epoch, profile). `profile_fingerprint` takes the application's profile bytes instead of Detent's `Limits`, and hashes through OpenSSL's EVP (the same SHA-256; the `sha256` library's `SHA256` class and OpenSSL's `SHA256()` function collide once both are visible). Detent's binding is `src/detent_authority/peers.hh`: `AuthorityPeers` (`Peers<Node>`) and `peer_configuration(NodeOptions)`, which fingerprints `semantic_profile(application)`.
- **TLS in `reactor`.** `detent_transport/tls.hh` became `reactor_tls.h` (target `reactor::tls`, when OpenSSL 3 is found): `reactor::tls::Options`, `Role::{Peer, Server, Client}`, `configure()`, and `bind_hostname()` (the publisher-client binding). The peer transport, Detent's delivery client and configuration, the relay, `detentd`, and the HTTPS health client all use it, and `include/detent_transport/tls.hh` is gone. Detent pins `reactor` itself, ahead of `http` and `cluster`, so a managed-agent-only build has it too. The HTTPS client (`detent_transport/https_client.hh`) stays in Detent until the rest of step 6.
- **Tests.** `consensus` takes the transport tests that do not need Detent's node (codec, TLS sessions over sockets and pipes, certificate authentication, ownership, RPC observation) with the certificate fixture, and adds a test that instantiates `Peers` over a minimal host: 14 tests pass. Detent keeps `test_transport_peers` (three voters over its node, with and without completion storage), `test_transport_https`, and the multi-voter process tests.

Acceptance: `reactor` 5 tests (with the new TLS test) and `consensus` 14 pass on macOS. Detent with every option on builds and, run serially, passes 174 of its 175 tests (181 before, less the 6 that moved); the remaining one is the in-progress `test_local_paged_compaction`, which does not compile in either state.


### Compaction integration (2026-10-13)

The formerly excluded `test_local_paged_compaction` now builds and passes against the extracted libraries (`6a03b42`). Native overlay/compaction checks pass 2/2 in 2.31 seconds; ASan/UBSan passes 2/2 in 2.85 seconds; the managed compaction check passes under TSan in 2.62 seconds. Branch-only, leaf-only and mixed replacements retain pinned readers and reclaim evacuated packs through the collector. This closes the specific compile failure recorded above; complete application and platform qualification remain separate gates.

The complete native application build and all 175 serial tests pass in 721.04 seconds with compaction included. This run uses the frozen `6a03b42` application binaries and the extracted sibling libraries; the subsequent shared-finalizer refactor has separate focused sanitizer qualification.

### SHA-256 header composition

Paged plan and candidate-validator headers name the hasher with an elaborated class type, so they compile after OpenSSL declares its global `SHA256()` function. This preserves the existing hasher type and wire bytes. A namespaced hasher API in `Kronuz/sha256` would avoid the global class/function collision for future consumers while retaining the old class for existing callers.

### Changes from strata's Track C and storage work (2026-10-14)

strata's session changes these libraries for its own needs; each is a versioned commit in the library, and Detent moves its pins when it chooses. Findings that apply to Detent are in `completion/docs/PLATFORM_FINDINGS.md`.

- `completion` `64d958b`: `IO::sync_files(files)` makes many files durable with one device flush where the platform allows it (macOS: `fsync` each, one `F_FULLFSYNC`; Linux: writeback started on all, then `fsync` each; FreeBSD unchanged). Additive: the default calls `File::sync` on each. A clean clone of Detent at `dcd729a` with every option on (authority, distributor, managed agent, relay, benchmarks, examples, tests), `completion` overridden to `64d958b`'s tree, builds and passes 191 of 191 tests run serially (1,025 s).
- `consensus` `afd456d`, `ec95656`: `Repair{request, boundary}` and an application epoch on `LocalCheckpoint` and `CaptureMetadata` (trailing fields, default 0); additive for Detent's fixed groups.
- `consensus` `188d196`: the randomized simulation harness and its mutants (tests only).
- `consensus` `0307308`: pre-vote, the leader-contact guard, and CheckQuorum, for fixed groups too, and the `DTC2` codec (`DTC1` frames are rejected). This changes Detent's wire and election behavior; Detent adopts it in the coordinated commit that moves its `consensus` pin, with strata's Track C (`docs/design/track-c.md` in strata) fixing every `DTC2` tag first.
- `consensus` `f050b7a`, `3b20cef`: membership in the log (`EntryKind::Configuration`, `ChangeMembership`, learners, single-voter changes, replacements); a checkpoint keeps `Configuration` entries in its suffix. Fixed groups run the same code with a founding membership that never changes; their storage bytes are unchanged.
- `consensus` `90d3df0`: `TransferLeadership{target}` and `TimeoutNow` (`DTC2` tag 8). Useful for draining a node.
- `consensus` `080e1c1`, `91de8ea`: witnesses (`2 + witness`), and `AppendRequest::data_matched` (a trailing field). The `DTC2` control frame's fixed header grows from 68 to 76 bytes, so `control_frame_bound` changes.
- `consensus` `573bfce`: `RFTINIT2` (founding roles; written only when a founding member is not a voter, so a fixed group's record is unchanged) and `RFTJOIN1` (joiners); `Worker::found(membership)` and `Worker::membership()`.
- `consensus` `e39abee`: limits in `Configuration` entries, checked by position (R16). `decode_membership` drops its limits parameter, and `checkpoint_detail::maximum_size()` without arguments is the decoder bound (the ceilings); reservations keep `maximum_size(limits)`.
- `consensus` `dd82c06`: a libFuzzer target over every decoder; `RFTINIT2` naming only voters is refused.
- `consensus` `056fcb7`: `RFTSNP02`, a snapshot descriptor with the base membership; a fixed group's descriptor stays the 72-byte `RFTSNP01`, so Detent's transfers are unchanged.
- `consensus` `2f8e6e2`: a leader refuses pre-votes and ignores vote requests without the transfer flag. Before it, a node rejoining a 3-voter group with an up-to-date log won a pre-vote quorum with the leader and deposed it. This fixes `0307308`'s pre-vote for fixed groups too; `TimeoutNow` is resent each heartbeat during a transfer.
- `consensus` `6cc0d88`, `7c60f58`: a witness leader hands off only to a data voter that has answered (witness groups only), and a T3 liveness harness in simulated time (`test/liveness_test.cc`) that Detent can run under its own timing profile.

- `consensus` `9f7d43e`, `c5b2223`, `1822fd7`, `aa4b11e`: the architect's C1 review.
  - **For Detent's fixed groups:**
    - `Timing{}` now defaults to R4's default profile: 100 ms heartbeat, 500 ms RPC timeout, 1,000–2,000 ms elections. That is Detent's authority profile, but code relying on the old 20/40/100–200 defaults must pass its timing explicitly.
    - `SnapshotReleaseReason` gains `Removed`, so an exhaustive switch over it needs a case.
    - The Worker accepts a snapshot from any authenticated incarnation, not only a founding voter.
    - `can_change_membership()` is false during a transfer.
  - **For membership groups:**
    - Learners and joiners vote.
    - A joiner's snapshots name their base membership.
    - A group's limits stay within its node's.
    - Removed members are pruned.
    - New members join as learners.
  - **Compatibility:** `test/compat_test.cc` and `tools/compat.sh` check, in both directions, that today's code reads `b7073bb`'s stores and that `b7073bb` refuses the new formats.
- `consensus` `6af73ae`, `7e23145`, `24d5399`, `906383c` (2026-10-15): C4's D15 and R19, all additive with today's behavior as the default.
  - `Core` takes a defaulted `Flow{pipeline, window_bytes}`; `pipeline = 1` (the default) is today's one Append RPC in flight per peer. Detent's fixed groups need no change.
  - `Receive` gains a defaulted `more` (merged appends from one leader); `Event` keeps its alternatives, so exhaustive visitors are unaffected.
  - `WorkerLimits` gains `follower_batch_bytes` and `follower_batch_entries`, both 0 by default (no holding).
  - The transport's control lane queues appends carrying entries apart from control frames, bounded by `PeerOptions::pipeline_frames` and `window_bytes` (defaults 4 and 4 MiB), and always sends control frames first; `PeerOccupancy` gains `queued_data`. A host that relied on more than four queued appends per connection now sees drops past the window instead.
- `consensus` `fcc18e6` (2026-10-16): C4's D18, windowed snapshot transfer, additive with today's behavior as the default.
  - `SnapshotLimits` gains a defaulted `window` (1 to 256 chunks of 64 KiB); 1 is today's one chunk per round trip, so `SnapshotLimits{format, bytes}` call sites are unchanged.
  - `Worker::source_chunk` takes a defaulted index (the i-th chunk read ahead); `snapshot_window()` is new; `snapshot_buffer_capacity()` grows with the window.
  - A Credit covers every chunk up to the one it names. With a window of 1 only one chunk is outstanding, so the frames are byte-identical and old and new nodes interoperate.
  - `SnapshotChannel`'s timeout now measures inactivity: each accepted chunk resets it, and it stops once the final chunk arrives. A transfer longer than the timeout that keeps moving no longer fails, and a slow install after the last chunk is not cut off.
- `consensus` `c752657` (2026-10-16): C4's R19 append cap, additive with today's behavior as the default.
  - `Flow` gains a defaulted `frame_bytes` (0: `rpc_bytes` stays the cap). A host that sets it bounds each append's payload; an entry above the cap still goes alone. `Flow{pipeline, window_bytes}` call sites are unchanged.

- `scheduler` `3ec5d7d` (2026-10-16, verified against a full Xapiand build): drops its vendored `random.cc`/`random.hh` and links `random::random`, reusing the target when a consumer already made it available. Detent pins both `random_lib` and `scheduler`; today it links two definitions of `random_real`, `random_int`, and `random_time`. When Detent bumps the `scheduler` pin, make `random_lib` available first: scheduler then reuses its `random::random`, while the other order makes scheduler fetch `random` itself and `random_lib`'s `add_library(random ...)` collide with that target.
- `boolean-parser` `ac248d1`, `time-point` `e024c28`, `cartesian` `34bf069`, `md5` `27dd372`, `ctrie` `89b7e4d` (2026-10-16): bug fixes with tests (operand order and unbounded depth; a nanosleep redefinition; `Cartesian::operator^=` and a mutating scale; unaligned MD5 loads; a leaking pragma). `cartesian` removes the member `operator*(double)`; `c * s` now resolves to the existing free overload, which returns a copy.

`DTC2` tags 1–8 are fixed (votes, appends, snapshot responses, pre-votes, `TimeoutNow`). The rest of `DTC2` is not yet: the frame header with group id and incarnations (C1, C4), coalesced heartbeats, `Wake`, `LeaderGone`, `Ping`, and `Pong` (C2), and window updates and snapshot chunk frames (C4). The coordinated commit that moves Detent's `consensus` pin waits for them, since a tag added later bumps the version. When it lands, Detent's fixed groups need no code change beyond the `decode_membership` and `maximum_size` call sites, if Detent calls them.

### Retained checkpoint validation (2026-10-17)

Local consensus commit `c2cbf2e` shares retained-entry validation between Recovery and incremental checkpoint encoding. Existing history remains encodable after lowering operating limits, and malformed configuration payloads fail validation. The constructor still scans no entries, step budgets remain bounded, and public APIs and encoded bytes are unchanged. Seven relevant library suites pass; Detent has not adopted this unpublished commit. The broader retained-state funding and allocation findings remain in [DEPENDENCY_UPGRADES.md](DEPENDENCY_UPGRADES.md).

Detent's synthetic election ticks now derive from configured `Timing` rather than assuming the old 100 ms minimum. Library adoption uses the current defaults; it does not preserve the old profile through a compatibility override.

Local consensus commit `ef30b4f` guards committed-delivery subtraction when retained entries exceed the current RPC byte limit. It emits an oversized first entry alone and preserves following entries for the next delivery. The before/after regression and 200-seed simulation qualify this resource correction without changing APIs or formats; Detent has not adopted the unpublished commit.

### Durability-modes additive-commit gate, October 19

Accepted with [shared decision 7](SHARED_LIBRARY_DECISIONS.md#7-the-durability-modes-strata-designdurability-modes-2026-10-19). At each additive library commit, move that immutable revision into a separate scratch Detent manifest, with a coherent transitive dependency closure. Enable authority, relay, managed agent and distributor explicitly, alongside tests; their defaults do not enable every product. Record Detent and library SHAs, compiler/SDK/build flags, the exact commands, pass/fail/skip counts and raw log locations here. Test each step before relying on the next; a moving sibling checkout, library CI or eventual combined head does not qualify the step.

As the corresponding APIs arrive, include exact v2/v3 vectors and invalid-field refusals, adjacent-version elections/appends/checkpoints/blob transfers, unchanged synced membership/batch/checkpoint bytes, old-state recovery and receipts, failed-barrier fencing, and rejection of `written` on the per-group backend. Verify unsupported formats preserve storage without automatic rejoin. Keep native macOS/Linux/FreeBSD and sanitizer evidence distinct from paired cost measurements. Respect host holds and use frozen measurement arms. No new implementation result is claimed by this agreement.

| Additive step | Candidate revision | Detent suite evidence | Adoption state |
| --- | --- | --- | --- |
| Durability-modes wire/API steps | Not landed at reviewed design `50245a1` | Pending each immutable implementation commit | Design contract agreed; no production pin change |

### Consolidation coordination, October 19

The [Detent response](CONSOLIDATION_RESPONSE.md) agrees to the ten requested audit items and defines the owner-lock migration. CL-17's HTTP allocator forwarding and CL-18's canonical resource adoption already have isolated implementations; finish those components and their qualification rather than create another extraction. The resource implementation is published and its application integration remains isolated at `33d36bf`. The audit's main-branch observation that Detent has no allocators link remains true until qualified production adoption.

Any ownership change must reach Journal's internal owner acquisition and direct host storage sessions, preserve file-bootstrap accounting and run the multiprocess legacy/new contention matrix. Pure aliases cannot preserve the different bool/manual and owning-lease admission APIs; compatibility facades share one engine. New test-filesystem failures are investigated without weakening crash semantics. Apply the additive-commit scratch gate above and keep implementation evidence separate from these design positions.

## Additional isolated qualification, October 19

The unchanged HTTP candidate passes all 14 library suites under LLVM 23.1.2 ThreadSanitizer, including server TLS, both clients and authentic held completions. The bounded MessagePack candidate passes the default-DOM four-test suite natively and with ASan/UBSan, object-size and pointer-overflow checks on Linux and FreeBSD. These are library checks, not cross-platform Detent product qualification. [HTTP evidence](../benchmarks/http-client-preliminary-20261019.json) and [MessagePack evidence](../benchmarks/msgpack-extraction-preliminary-20261019.json) record the profiles and scope. Xapiand, public immutable pins and paired cost checks remain open.

A separate Detent scratch build at `cce7a53`, with only the reactor pin changed from `e19afd9` to `1c37def`, builds every product and passes all 191 selected functional tests in one invocation. The Release profile uses `-O2 -UNDEBUG`, with authority, relay, distributor, managed agent, examples and benchmarks enabled. The source closure is the frozen public adoption baseline, not the resource or HTTP extraction candidates. The command is `ctest --test-dir BUILD -E '(test_system_|test_relay_network_|test_load_fanout)' --output-on-failure -j 1`. Fleet, relay-network and fanout tests are excluded. Targeted UDP exception, throwing diagnostic hook and persistence-failure recovery checks remain required before adoption; this result changes no production pin.

HTTP's isolated full server/client library profile now passes 14/14 natively and 14/14 under ASan/UBSan on each of Linux and FreeBSD. The default published dependency graph and downstream products remain separate gates. Its POSIX fixture needs a braced construction for GCC and an explicit networking type header on FreeBSD; these changes do not alter transport behavior. [Platform evidence](../benchmarks/http-client-preliminary-20261019.json) records those corrections.


## Classic Raft application retry and callback containment, October 20

The [component evidence](../benchmarks/raft-apply-retry-qualification-20261020.json) records an isolated Detent all-products build and 192/192 selected functional tests, plus reactor 8/8 and cluster 4/4 native/sanitizer suites on macOS, Linux and FreeBSD. The generic retry drain belongs in classic `cluster::Raft`; UDP owner-loop exception containment and repeat-timer rearming belong in `reactor`. Detent supplies its real persistence regression and existing logger diagnostic hook, without a private runtime or retry engine. Durable authority consensus transport is separate and unchanged.

The candidate preserves public APIs, quorum behavior and wire bytes. Its delegate contract requires atomic-on-throw or idempotent application. The separately recorded Distributor post-persist preparation gap remains open. These local-override checks precede immutable publication and public dependency-closure qualification; they do not move production pins or close sustained/fleet gates.

## Shared HTTP access logging, October 20

HTTP-log `962c1c84` provides the optional bounded metadata preset used by classic Distributor; Xapiand's default detailed renderer and the shared method/status palettes remain intact. Its eight-job CI passes across Linux, macOS and FreeBSD, including sanitizer/TSan jobs. Logger `8c5afcd` already supports severity tint, so no logger/runtime upgrade or Detent-specific renderer is needed. Catalog `d26916f` records the API and actual `http_log::http_log` target.

Detent's final closure pairs HTTP `6a07f99` and HTTP-log `2f066ee` to preserve error mapping after partial responses. `BufferedResponse::response_ended()` exposes the existing Asio completion flag; successful end, rather than assigning a status, determines whether mapping/fallback remains necessary. This source-compatible virtual extension requires a complete consumer rebuild. Both eight-job CIs pass, along with native/sanitizer HTTP 9/9 and HTTP-log 2/2 suites and five real TCP wrapped/unwrapped comparisons. Catalog `d894c23` records the coupled requirement.

The [validated completion-observer blueprint](HTTP_LOGGING_ARCHITECTURE.md) places authentic response completion in `http` and rendering in the existing `http-log` decorator. Authority and relay adoption waits for that component; their deferred/streaming settlement, admission leases and application callbacks remain unchanged. [Qualification evidence](../benchmarks/xapiand-logging-preliminary-20261020.json) records the clean public-pin all-product build with 35 verified populated revisions and 4/4 selected tests. The [accepted exploratory cost run](../benchmarks/logging-cost-comparison-20261021.json) records CPU, sampled RSS and log conservation. Component review accepts the explicit +57,552-byte (2.68%) distributor file-size exception. Filtered CPU remains within the observed variation; verbose logging adds about 47–47.5 CPU seconds per million polls. Allocation, peak-memory and async-observer qualification remain pending.

## Btree ownership and mutation, October 21

The isolated [combined copy/insertion repair](../benchmarks/btree-copy-insertion-qualification-20261021.json) passes native/sanitized layout and mixed-mode gates on macOS, Linux and FreeBSD. With only its source override, the original failing Detent publication candidate passes the complete PUT/PATCH allocation-failure sweep under Linux LeakSanitizer, plus application retry and diff-cache tests. Production still pins `dd984d9`; no uncommitted source override is presented as immutable-pin qualification.

Compact ordered copy construction replaces topology cloning, whose populated/pruned retained bytes regress in the [matched foundation comparison](../benchmarks/btree-foundation-cost-20261021.json). The shared image/transaction engine belongs in cpp-btree. Its next erase/merge component must preserve actual const-key lifetimes and provide allocation-free bounded reclamation through an ownership-taking cursor, with one persistent cursor per active Detent owner. Existing resource accounting stays canonical; no second retirement ledger or application-local map implementation is added.

Xapiand `c625977` does not consume cpp-btree in the inspected source/CMake or built dependency metadata; its Xapian B-tree is unrelated, so its gate is N/A for this library. Strata `88a56e7` consumes it only in the opt-in buffer research target. Final publication must record the repaired immutable revision, matched mutation/copy costs and real consumer qualification; current source-only passes do not close erase/merge or release gates.

The [erase/reclamation follow-up](../benchmarks/btree-erase-reclamation-preliminary-20261021.json) receives source GO after the same four architecture checks. Erase and insertion now share sparse mutation images and materialization; ordinary clear and the owning bounded cursor share one destruction walker. Detent's shared definition arena and active command owners retain one cursor each, using the existing retained-value accounting until final deletion. Native/sanitized macOS library profiles and four consumer tests pass; expanded allocation-denial owner-transition fixtures also pass. Final platform, mixed-mode, failure, footprint and matched-cost gates remain open. No incomplete library source is pinned or published.

The [final erase/drain functional archive](../benchmarks/btree-erase-reclamation-qualification-20261021.json) passes the complete three-platform native/sanitizer/mixed-mode matrix. Composing its headers with the current-pin collector migration and classic-publication atomicity candidate passes seven Linux ASan/UBSan/LeakSanitizer tests with no leak report. Direct raw-node mutation clients must migrate to public containers; the removed node helpers are an explicit internal source incompatibility. Cost, owner-footprint and immutable-pin gates remain open, so production adoption is still withheld.

## Independent btree repairs, October 22

The transaction draft is excluded on its measured mutation cost. cpp-btree publishes independent range (`13370b0`), comparator (`fbecd36`), first-root (`2e93b45`) and compact-copy (`5aadde0`) corrections. The [copy qualification](../benchmarks/btree-isolated-copy-20261022.json) rebuilds isolated sources across all three platforms and records current-pin consumer failures as failures. The full publication sweep still finds native growth leaks; no composed release or production pin adoption is inferred from the copy fix.

The [release risk evaluation](BTREE_RELEASE_RISK.md) replaces the assumption that the broad transaction design is mandatory. Detent needs legal ordinary mutation and safely destructible rejected candidates for its actual types; stronger generic guarantees and unused overload cleanup may be deferred. The standard-map alternative remains isolated and has a measured retained-memory cost. Constructor constraints for nested nondefault allocators remain a distinct library compatibility issue. No parallel production map or retirement engine is added.

### Independent btree adoption, October 22

Detent adopts cpp-btree `5aadde0c1b415b33c48d6515704ae7bdc32bb259` after a fresh immutable fetch, an all-product Release build and 207/207 registered tests (640.81 seconds, two jobs). Authority, relay, managed agent and examples are enabled; benchmarks and optional bindings are disabled. The four independent fixes and their narrower sanitizer/cost evidence are recorded in [dependency upgrades](DEPENDENCY_UPGRADES.md). This adoption does not include the pending compact-root growth repair or close the publication failure-injection gate.

### Guarded-growth adoption, October 22

Detent uses the library-owned `btree::btree` target at immutable `19db54c0baa6747f9d8ba9a4052820c7b554cdb5` and removes its parallel CMake header wrapper. The renamed repository retains old Git redirects and the `cpp_btree::cpp_btree` alias. All-product Release build, 207/207 native tests (639.30 seconds) and seven hosted library jobs pass. The composed publication leak gate passes independently; remaining mutation paths and the publication atomicity integration stay separate release work.
