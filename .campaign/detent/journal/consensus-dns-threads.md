# consensus-dns-threads: journal

State: waiting on jobs 261118-032432-839288 261118-032434-fe80ab 261118-032436-185354 261118-032500-0214d2 261118-032501-24b2c1 261118-032504-295c83 261118-032511-714980 261118-032533-1428d3
Quest: .campaign/detent/quests/consensus-dns-threads.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs at consensus:ad10797 (resolve.h + resolve_test.cc). Local run (macOS, -O1): all five
cases pass; shutdown waited 307 ms for a resolver stuck 300 ms; exit waited 206 ms for a
200 ms resolver; rebuilds peaked at 16 threads.

Jobs: 839288 plain (A1,A3); fe80ab TSan (A2); 185354 ASan+UBSan (A2); mutants
0214d2 unbounded (A1), 24b2c1 queued-kept (A3), 295c83 detach-at-destruction (A2),
all --fails-without; 714980 suite (A4); 1428d3 peers_test hung-resolver with
process_bound 1 (--fails-without, A4: the bound keeps a hung lookup from queueing others).

## Next

1. Read the jobs; paste `campaign brief`'s table into Evidence.
2. Correctness review.

## Blocked on

The lead, for three files outside my set (work continues by direct build):
1. CMakeLists.txt (consensus-close-fenced's now): test/transport is not globbed. Register
   `resolve` in both `foreach(t ...)` lists of transport tests (build + `add_test`, TIMEOUT 30).
2. docs/mechanisms/peer-dns.md, "The lookup and its thread": still says one detached thread
   per lookup. Text for it is under Result.
3. test/transport/dns_mutants.sh `shared-thread`: its anchors (`#include <thread>\n`,
   `std::thread(std::move(work)).detach();`) no longer exist. Equivalent mutant: replace
   `static constexpr std::size_t process_bound = 16;` with `= 1;`.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

Pending jobs. consensus:ad10797: `LookupThreads` in include/consensus/transport/resolve.h
owns lookup threads (bound 16 for the process), `Lookup::start`/`abandon()` unchanged for
links.h and peers.h; test/transport/resolve_test.cc (cases delivers, abandoned, shutdown,
rebuilds, plus an at-exit join check).

Proposed text for docs/mechanisms/peer-dns.md, "The lookup and its thread" (outside my
file set), replacing its second and third paragraphs:

> `Lookup::start` queues the lookup on the process's `LookupThreads`, which runs at most 16
> (`process_bound`) at once and starts a thread only when a lookup waits and none is free;
> a thread returns once the queue is empty. `getaddrinfo` cannot be cancelled, so the
> lookup does not use asio's `async_resolve`, which runs every lookup of an `io_context` on
> one thread: a hung lookup holds one of the 16, and only 16 hung at once stall the rest.
> `stop()` and the destructor call `abandon()`: a queued lookup leaves the queue with its
> resolver unstarted; a running one's delivery is cleared under the lookup's mutex, and its
> thread drops the answer when the resolver returns. The process's `LookupThreads` is
> destroyed at exit: it drops the queue, delivers nothing more, and joins every thread,
> waiting for each running resolver to return (a stuck `getaddrinfo` delays exit by up to the
> system resolver's timeout; resolve_test measures 307 ms for a resolver stuck 300 ms).

## Rulings I made

1. The owner is the process's `LookupThreads` (resolve.h), not each transport instance. A1
   rebuilds the transport 1,000 times over a resolver that never answers: an owner per
   transport that joins at stop() would block the first rebuild for good (and peer-dns.md
   requires stop() not to wait on DNS), and one that does not join leaves the threads
   unowned again. `Lookup::start`'s signature and abandon() are unchanged for links.h and
   peers.h. `LookupThreads(bound)` is also usable as an owned instance (the tests use it).
2. Bound 16 (`LookupThreads::process_bound`): a transport has at most one lookup per peer
   name in flight; 16 names resolving at once covers a cluster's peers, more queue in order.
   The cost: 16 resolvers stuck at once stall later lookups in the process until one returns.
3. Threads start on demand and return when the queue is empty (no idle threads: the idle
   cost docs count threads); a returned thread is joined by the next start that needs a
   thread, or by destruction. At most `bound` std::thread objects exist.
4. Stuck resolver at shutdown: destruction (the process's at exit) drops the queue, stops
   delivery, and waits for each running resolver to return, then joins. getaddrinfo cannot
   be cancelled; waiting is the only way to join. A stuck getaddrinfo delays exit by up to
   the system resolver's timeout.
5. A1's "transport rebuild" is driven at the Lookup level, as Links::stop and ~Peers do it
   (start, then abandon), so the test needs no TLS fixture or Worker.
6. A1 "fails at main": main's header has no LookupThreads, so the test cannot compile
   against it; the unbounded mutant (bound check removed: one thread per waiting lookup) is
   main's thread growth, and is the --fails-without job.
