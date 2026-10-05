# Authenticated authority with completion-based IO

**Status:** Validated Architectural Blueprint, Architect GO for autonomous implementation as one complete component. Review follows the completed component, with correction rounds as needed. The bounded application/runtime component has source GO; final qualification is recorded in [AUTHORITY_ARCHITECTURE.md](AUTHORITY_ARCHITECTURE.md).

The next deliverable is a real three-process authority using authenticated sockets, strict durable replication, snapshot catch-up and asynchronous client responses. Native IO completions are preferred over blocking disk threads. Coroutines express waits; platform backends provide completion semantics. A responsive reactor does not shorten a flush or authorize an acknowledgment before required persistence completes.

```text
Publisher HTTPS -> Bounded request/reply coroutines --+
Peer mTLS ------> Control and snapshot connections --+
                                                     v
                                          Authority owner strand
                                          Core, State and Store
                                                     |
                                           Owned IO operations
                                                     |
                                    Native completions / fallback
```

One strand owns mutable authority state and settles network and storage completions. No backend accesses Core or knob state from a worker thread.

## Scalability and execution

Use the existing Asio coroutine runtime for network awaits. Introduce reusable completion IO below journal operation sequencing. Refactor append, sealing, publication and reclamation into resumable operations shared by synchronous and completion-driven execution; do not offload `Worker::run_one()` wholesale or duplicate durability algorithms.

An admitted IO operation owns its token (owner generation and sequence), buffers, file handles, pins and reserved completion capacity. Successful submission produces exactly one terminal completion; rejection performs no operation. Ownership survives cancellation until terminal completion. Only the owner strand advances accounting and the durable frontier or emits `Persisted`.

Allow one mutating transaction per Store and bounded retained work slots. Pending funded consensus persistence has priority over starting another preparation or reclamation quantum. Preparation and readback retain bounded chunks and yield at safe storage boundaries. Pressure cannot discard persistence, application or activation completion.

| Platform | Preferred backend | Explicit fallback |
|---|---|---|
| Linux | `io_uring` reads, writes, sync and supported namespace operations, with completion notification integrated with Asio | Unavailable native backend or unsupported operations |
| FreeBSD | POSIX AIO reads/writes and `aio_fsync`, with `SIGEV_KEVENT` | Unsupported filesystem and namespace operations |
| macOS | Public SDK/runtime-supported AIO and completion notification | `F_FULLFSYNC` and unsupported namespace/metadata operations |

Native submission can involve kernel threads. The objective is to keep blocking operations off the owner/reactor path, rather than promise that no thread exists inside the OS. Start with one bounded fallback worker for the single Store. Expose backend capabilities and fallback counts in status, and select fallback before execution or after a rejection proving nothing was submitted.

Linux documents asynchronous [fsync](https://www.man7.org/linux/man-pages/man3/io_uring_prep_fsync.3.html) and [rename](https://www.man7.org/linux/man-pages/man3/io_uring_prep_renameat.3.html). FreeBSD documents [AIO synchronization](https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libsys/aio_fsync.2) and [kqueue completion notification](https://raw.githubusercontent.com/freebsd/freebsd-src/main/share/man/man3/sigevent.3). Apple's [full-flush distinction](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsync.2.html) and [AIO implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/kern_aio.c) require preserving `F_FULLFSYNC`; ordinary asynchronous filesystem synchronization does not establish an equivalent full-flush contract. [Epoll readiness](https://www.man7.org/linux/man-pages/man2/epoll_ctl.2.html) cannot be used to await regular-file durability.

Measure CPU pauses separately from disk waits. Make receiver-local bundle encoding incremental, then measure retained-suffix capture against the timer budget. If payload copying exceeds that budget, introduce immutable payload sharing within the strict module. `co_await` alone does not remove synchronous encoding work.

## Durability, cancellation and scheduling

1. Appending bytes is not durable completion. Finish file synchronization, frontier publication and required directory/full-flush barriers before returning `Persisted`.
2. Preserve namespace ordering and directory synchronization around publication. A successful rename alone is insufficient. [Linux fsync semantics](https://www.man7.org/linux/man-pages/man2/fsync.2.html).
3. Short reads and writes advance only by the reported count. Failed or uncertain mutations fence Store and Worker with conservative accounting.
4. Cancellation cannot authorize buffer destruction, refunds or handle reuse before the original terminal completion. [Io_uring cancellation semantics](https://www.man7.org/linux/man-pages/man3/io_uring_prep_cancel.3.html).
5. Never retry an uncertain namespace mutation through a different backend. Fallback selection must distinguish an unsupported submission from an operation that may have executed.
6. Shutdown stops admission and drains or cancels owned operations while retaining the owner lock. A process shutdown deadline cannot manufacture clean completion.

Keep Core's pending-persistence safety behavior. The reactor can service sockets, retain bounded messages and observe clocks while IO is outstanding, but it cannot invent heartbeats outside Core or acknowledge undurable entries. A sufficiently stalled disk can still cause an election. Existing protected reservations, maintenance debt and reliable application completions remain obligations across suspension.

## Reusable boundaries and migration

1. Completion IO owns platform operations and delivery, with no Detent or Raft types.
2. Journal/Store own resumable durability transactions, verification and accounting, preserving existing formats and a synchronous driver of the same algorithms.
3. Strict Worker owns funded event admission, storage continuations, reliable application/activation and bounded scheduling.
4. Peer transport owns framing, authenticated sessions and coroutine lifecycles, with no knob semantics.
5. Authority State retains deterministic transactions, sessions and images without sockets or platform IO.
6. The service adapter owns publisher authorization, request/reply capabilities, configuration and operational status.

Add owned asynchronous responses to the HTTP layer while preserving its synchronous handler contract. Retain admission until reply or disconnect, eliminate blocking future waits from the authority path, and release disconnected response waiters without canceling admitted consensus work. Do not pin an entire historical State for each slow reader; capture only requested definitions and revisions.

Exchange the canonical semantic-profile fingerprint during peer admission. Reject incompatible limits before replication, with `DTABAT02` remaining the durable backstop. Fixed membership stays fixed. A three-voter group starts with fresh local stores and identical explicitly configured membership; do not edit a one-voter store into three voters or clone its vote/identity. Migration stops writers and exports/imports into a new group and restore epoch.

Legacy cluster discovery, wire formats and Xapiand behavior remain intact. Additive HTTP/reactor interfaces require existing consumer regression gates. Xapiand's migration to strict durable execution remains separate work.

## Security and bounded transport

Use mutual TLS for peers and server-authenticated TLS plus existing producer credentials for publishers. A trusted peer capability is created only after authentication binds the certificate identity to a configured voter, cluster, configuration and restore epoch. Disable early-data mutations and never log secrets.

A versioned bounded hello carries a fresh connection identity and semantic-profile fingerprint. Reject unknown peers, duplicate identities, incompatible versions and mismatched groups before replication payload allocation. Use separate control and bulk connections per peer so snapshot frames cannot obstruct votes and replication responses. Implement bounded codecs for strict Core messages and reuse the existing snapshot transfer protocol; this does not select the future public transaction/distribution encoding.

Preserve peer/session/term/flight correlation. Socket write completion is never an Installed ACK. Retain one bounded inbound message while Core is busy, stop further reads on that stream, and give partial frames and stalled writers deadlines. Bound connections, TLS handshakes, headers, frames, per-peer messages, waiters and ingress/egress bytes before allocation.

Keep producer namespace authorization separate from peer identity. Followers may return configured leader hints, without forwarding credentials to request-selected endpoints. Start with separate 16 MiB ingress and egress budgets, bounded TLS handshakes, and one in-flight request per HTTP connection. Configure conservative connection counts and deadlines explicitly.

## Complete-component acceptance gates

1. Run existing durability fault schedules against synchronous and completion-driven sequencing. Inject delayed, failed, canceled and partial completions around append, sealing, manifest replacement, directory barriers and reclamation. Compare recovered acknowledged prefixes and accounting.
2. Hold storage completions while sockets and timers continue. Prove no premature ACK, application delivery, slot reuse or accounting refund; measure timer delay, CPU turn length, queue occupancy and maintenance progress under publishing and snapshot traffic.
3. Exercise three real TLS-connected processes with independent stores: CAS, strong reads, leader replacement, partitions, reconnects, nonempty follower snapshot installation, lost responses, restart and stale-waiter overwrite.
4. Test wrong certificates/IDs/epochs/profiles, oversized/truncated frames, duplicate sessions, slow readers, handshake floods and disconnects around publication. Preserve bounded queues and durable state.
5. Run hundreds of simultaneous publishing clients without a blocked thread per request. Measure ACK latency distributions separately from amortized grouped throughput, including retries, cancellation, authorization and egress pressure.
6. Compile and exercise each native backend on its target OS/filesystem. Report native operations and fallbacks separately; untested backends remain unqualified.
7. Repeat 100k/500k and override-heavy workloads, separating application, TLS/codec and storage CPU, barrier latency, grouping delay, ACK latency, event-loop delay, RSS and actual IO bytes. Convert CPU demand into cores at a stated request rate; dollar/power claims require matching pricing or energy evidence.

Implement in dependency order and commit coherent changes autonomously. Return for one consolidated review of the usable authenticated authority service. Relay fanout, agent subscriptions and immutable local publication follow this component.
