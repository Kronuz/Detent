# consensus-dns-threads: journal

State: waiting on jobs 261118-063756-2fdd9f
Quest: .campaign/detent/quests/consensus-dns-threads.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review fix round 1 (correctness review: CHANGES, F1 and F2) at consensus:abc01bb (on
consensus:9fc2405, origin/main merged as the brief asked). Pushed.

F1=261118-063756-2fdd9f (mutant destroyed-at-exit under ASan+UBSan, --fails-without; at
head, asan 261118-055338-321238 is clean). It replaces c5f7dd, whose mutant did abort (UBSan
"invalid vptr" in shared_ptr.h:475, exit 134) but whose runner only counted ASan reports as
caught; rt.sh now also counts "runtime error:". Lead: every other round-1 job came out right
(F2 67d3bb, mutants 38236d f6ac66 061f08, plain, TSan, ASan, probes, suite 301520). F2=261118-055341-67d3bb (mutant
delivery-outside-lock, --fails-without, case abandon_waits).

Jobs at abc01bb, scripts in consensus-wt/.scratch/dns-fix/rt.sh: e81e18 plain (A1,A3);
063276 TSan (A2); 321238 ASan+UBSan (A2, F1 throw clean); mutants c5f7dd F1 (A2), 67d3bb F2
(A3), 38236d unbounded (A1), f6ac66 queued-kept (A3), 061f08 detach-at-destruction (A2);
a9ddbb dns_mutants shared-thread (A4); reviewer probes b43a1c `run.sh stress`, 9f9883
`run.sh exit`; suite 055353-301520. Local plain and ASan runs: all cases pass, at-exit start
threw operation_canceled, abandon_waits waited.

1. Read the jobs; paste `campaign brief`'s table into Evidence; `campaign accept
   consensus-dns-threads correctness --confirm F1=<c5f7dd> F2=<67d3bb> ...` is the lead's.
2. Once consensus-close-fenced lands and CMakeLists.txt joins my file set: register
   resolve_test in both transport `foreach(t ...)` lists, rerun the suite.
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
