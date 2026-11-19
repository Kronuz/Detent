# consensus-dns-threads: journal

State: waiting on jobs 261118-214145-4748d9
Quest: .campaign/detent/quests/consensus-dns-threads.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Suite gate at e20721d failed (261118-212248-85299d): consensus_transport_resolve aborted
at shutdown's entry check `os_threads() == baseline` (line 227) after 5 s, under ctest -j4.
Investigating; one hypothesis per experiment.

consensus:d76b788 instruments the test: `settles()` replaces the bare count waits and on a
timeout prints the count over time and each thread's name and run state; the test names its
threads (warm-up, lookup, abandoner, opener). No product change.

Loop 261118-214145-4748d9 (--for A4, at d76b788): the full binary 1,600 times, 8 at once.

1. Read the loop's failures: which thread is surplus or missing, and how the count moved.
2. Fix the cause; merge origin/main (84608cc, groups_test.cc); verify + loop again.
## Blocked on

Nothing. Lead re-accepts correctness once the d0f5cbf jobs pass.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | 261118-193306-7979ae (suite at d0f5cbf); CI 44437166273 attempt 2 green |

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
