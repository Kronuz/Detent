# consensus-dns-threads: journal

State: waiting on jobs 261118-214447-666fec 261118-214449-5d9a15 261118-214507-d39df7 261118-214529-7441ed 261118-214531-9748fd 261118-214532-7ff35e 261118-214533-18c77e 261118-214535-e427d1 261118-214536-d96e0d 261118-214538-f02c6d 261118-214539-942f84
Quest: .campaign/detent/quests/consensus-dns-threads.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Suite gate at e20721d failed (261118-212248-85299d): consensus_transport_resolve aborted at
shutdown's entry check `os_threads() == baseline` (line 227) after 5 s, under ctest -j4.

Cause (test baseline, not product, not the bound): on macOS a joined thread can stay in
task_threads for a moment after join returns. main() read the baseline right after joining
the warm-up thread, so under load it counted that thread: baseline 2 while the process had 1.
Evidence: loop 261118-214145-4748d9 at consensus:d76b788 (instrumented test; full binary, 8 at
once) had 46 of 288 runs fail, every one "expected 2 (baseline 2, 1 before the warm-up
thread); count over time: 0ms:1", one thread listed (main). No surplus thread in any run.
(That job was superseded by the fixed loop's submit before it finished; tally read from its
slot.)

What changed (voids the correctness acceptance; test only, resolve.h untouched):
- consensus:d76b788 test/transport/resolve_test.cc: `settles()` replaces the bare count waits
  and prints the count over time and each thread's name and run state on a timeout; the test
  names its threads (warm-up, lookup, abandoner, opener).
- consensus:88c035b merges origin/main (84608cc, groups_test.cc only).
- consensus:f427934 resolve_test.cc: baseline = the count seen inside the warm-up thread less
  that thread; main waits for the system to reach it (5 s) and, without ASan/TSan, asserts it
  equals the count before the warm-up. The post-join waits print how long joined threads stay
  listed (input for the 1 s bounds, unchanged).

Jobs at f427934: loop 666fec (--for A4); fails-without loop 5d9a15 at d76b788; suite d39df7
(verify); plain 7441ed (A1,A3); TSan 9748fd, ASan 7ff35e (A2); mutants unbounded 18c77e (A1),
destroyed-at-exit e427d1, detach-at-destruction d96e0d (A2), delivery-outside-lock f02c6d,
queued-kept 942f84 (A3). Branch pushed for CI.

1. Read the jobs; if the loop's post-join listing times approach 1 s, derive the bound from
   them with headroom; paste `campaign brief`'s table into Evidence.
2. Lead: confirm the test-only change by job or send it back to review.
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

Flake (suite 85299d):

10. The baseline is a value, computed while the warm-up thread is alive (its count less one),
    not read after the join; equality with the pre-warm-up count is asserted only without a
    sanitizer, since ASan/TSan may start threads of their own with the first thread.
11. The 1 s post-join bounds stay: in the fixed loop's first runs joined lookup threads left
    the list in 0-1 ms; the loop reports the median, p99 and max over 1,600 runs.

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
