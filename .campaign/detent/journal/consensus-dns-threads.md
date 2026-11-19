# consensus-dns-threads: journal

State: waiting on jobs 261118-172630-494843 261118-172631-775cea 261118-172631-2a69bd 261118-172632-4f2e4b 261118-172633-aed45d 261118-172633-d869e1 261118-172634-4f4d2c 261118-172648-70f3f3
Quest: .campaign/detent/quests/consensus-dns-threads.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Resumed after consensus-close-fenced landed (consensus main 377e472). Merged origin/main
into the branch: consensus:8d24f40, clean (main's 14 files touch none of mine). Pushed for CI.

Gap 3 (c5f7dd "failed"): the mutant was killed, the runner miscounted. Its log shows the
destroyed-at-exit mutant aborting with UBSan "invalid vptr" (shared_ptr.h:475, exit 134),
but rt.sh then only counted ASan reports. rt.sh now also counts "runtime error:"; the
replacement 261118-063756-2fdd9f at abc01bb reports "caught (exit 134)", fails-without as it
must. Not a gap in F1's fix or test. (rerun reused it at 8d24f40: only merges since.)

Reruns at 8d24f40: plain 494843 (A1,A3), TSan 775cea (A2), ASan+UBSan 2a69bd (A2);
mutants F2 4f2e4b, unbounded aed45d, queued-kept d869e1, detach-at-destruction 4f4d2c;
suite 70f3f3 (--for A4, forced fresh: main's driver and host_transport changes).

1. Read the jobs and CI at 8d24f40; paste `campaign brief`'s table into Evidence.
2. Lead: re-accept correctness after main's 14 files (none in resolve.h's callers).
## Blocked on

Lead: CMakeLists.txt is not in my file set (`campaign get consensus-dns-threads files`), so
resolve_test is still not registered in the suite; A4's suite covers links/peers but not
resolve_test, which runs only through rt.sh jobs. Options: (a) add CMakeLists.txt to the file
set and I add resolve_test to both transport `foreach(t ...)` lists (recommended, one line
each); (b) land with resolve_test covered by the rt.sh jobs only.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |

## Result

Pending jobs. consensus:ad10797: `LookupThreads` in include/consensus/transport/resolve.h
owns the lookup threads (bound 16 for the process); `Lookup::start`/`abandon()` unchanged for
links.h and peers.h; test/transport/resolve_test.cc (cases delivers, abandoned, shutdown,
rebuilds, and an at-exit join check). consensus:ed241c3: docs/mechanisms/peer-dns.md
describes the bound, abandonment, and the join at exit with its measured wait;
test/transport/dns_mutants.sh `shared-thread` is now `process_bound = 1`.
## Rulings I made

Review round 1:

7. F1: the process's instance is immortal (`*new` in the function-local static) and closed
   by an atexit handler registered when it is built: the reviewer's shape, chosen because it
   is the only one where every start() after exit finds live state and throws
   operation_canceled; a shared_ptr or a pointer reset at exit would just move the dangling
   read. The destructor and the handler share one private `close()`; no public API changes.
   If atexit cannot register, building throws system_error (start() already throws it).
8. F2: case `abandon_waits`; a delivery parks on a gate, abandon() on another thread must
   not return within 200 ms and must see the delivery returned. Mutant is the reviewer's
   delivery-outside-lock edit.
9. N1 (std::exit on a lookup thread hangs): documented as a "must not" in resolve.h and
   peer-dns.md, not fixed. N6 reflowed. N2-N5 left to the owner/lead.

Lead rulings (messages): (a) the per-process owner with bound 16 is accepted, for the reason
in 1 below; (b) peer-dns.md and dns_mutants.sh joined my file set, done in ed241c3; (c) the
quest lands after consensus-close-fenced, which hands over CMakeLists.txt; until then the
suite's result is not evidence for resolve_test.

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
