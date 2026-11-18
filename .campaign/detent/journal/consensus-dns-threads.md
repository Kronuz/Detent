# consensus-dns-threads: journal

State: waiting on jobs 261118-033407-1586b4 261118-033409-40b51d 261118-033413-29a593 261118-033416-7584d2 261118-033419-9c57c8 261118-033422-d8a330 261118-033424-f89dfd 261118-033427-aa0263
Quest: .campaign/detent/quests/consensus-dns-threads.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs at consensus:ed241c3 (ad10797 code + ed241c3 peer-dns.md and dns_mutants.sh).
Local run (macOS, -O1): every case passes; shutdown waited 307 ms for a resolver stuck
300 ms; exit waited 206 ms for a 200 ms resolver; rebuilds peaked at 16 threads.

Jobs: 1586b4 plain (A1,A3); 40b51d TSan (A2); 29a593 ASan+UBSan (A2); mutants
7584d2 unbounded (A1), 9c57c8 queued-kept (A3), d8a330 detach-at-destruction (A2), all
--fails-without; f89dfd dns_mutants.sh shared-thread (process_bound 1) caught by
peers_test (A4); aa0263 suite (A4). The suite does not run resolve_test until CMakeLists
registers it (msg: lead ruling 3), so resolve_test evidence is the direct-build jobs only.
Cancelled as superseded: the ad10797 jobs (839288, fe80ab, 185354, 0214d2, 24b2c1,
295c83, 714980) and 1428d3 (inline bound-1, replaced by f89dfd).
## Next

1. Read the jobs; paste `campaign brief`'s table into Evidence.
2. Once consensus-close-fenced lands and CMakeLists.txt joins my file set: rebase, add
   `resolve` to both transport `foreach(t ...)` lists, rerun the suite (then it counts for
   A1-A3 too).
3. Correctness review.
## Blocked on

Nothing to decide. Waiting for consensus-close-fenced to land, after which the lead adds
CMakeLists.txt to my file set to register resolve_test (lead ruling 3).
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
