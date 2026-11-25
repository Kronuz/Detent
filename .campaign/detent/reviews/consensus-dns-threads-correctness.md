# consensus-dns-threads review: correctness

Commit: consensus:ed241c3bdd7fd366a339e0fad17bee8bb4b5075b
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261118-045508-5390a9 (plain), 261118-045510-15b2e8 (TSan), 261118-045513-018869 (ASan+UBSan):
  every resolve_test case passes at ed241c3. rebuilds peaks at 16 OS threads past the baseline,
  with 0 queued and 16 resolver calls. shutdown holds 304-310 ms for a resolver stuck 300 ms. Exit
  waits 199-208 ms for a 200 ms lookup and joins every thread. The test counts threads with
  task_threads, /proc/self/task or sysctl, not with `threads()`, so these are values.
- Mutants 261118-045515-48fe11 (bound check removed: 18 threads at rebuild 5),
  261118-045518-1bd27b (`if (queued_)` -> `if (false)`: queued() != 0) and 261118-045520-9559dc
  (running threads detached at destruction: held 0 ms). Each mutant removes the mechanism behind
  the criterion it is tagged for, and the case that catches it is the one named.
- 261118-045523-10aced: with `process_bound = 1`, peers_test fails, so peers_test depends on
  lookups running in parallel.
- CI run 44295829569 at ed241c3 is green except linux-tsan. Its one failure is
  `connection_test.cc:693` (slow_reader), which does not exercise DNS. Quest
  connection-slow-reader-flake tracks it.

## What I checked myself

Scripts are under `.scratch/review-consensus-dns-threads-correctness/` (`run.sh stress|exit`,
`stress.cc`, `exit_probe.cc`).

- 261118-052436-e9dbb9: stress.cc under TSan at ed241c3. Four callers contend on one
  LookupThreads(3) and start and abandon 24,000 lookups whose resolvers take 0-200 us. TSan is
  clean. At most 3 threads exist (bound 3). No delivery runs after abandon() returns. Every lookup
  that was not abandoned is delivered exactly once. Destruction with lookups queued and running
  delivers nothing afterwards. The same job builds a mutant that calls the delivery outside
  `lookup->mutex_`. The whole resolve_test passes against that mutant (not caught). stress.cc
  catches it, with 6 deliveries after abandon().
- 261118-052437-4b7723: exit_probe.cc under ASan+UBSan. An atexit handler registered before the
  process's LookupThreads was built calls Lookup::start after that object is destroyed. The result
  is a heap-use-after-free in `LookupThreads::start`. A delivery that calls `std::exit` on a lookup
  thread hangs the process; the probe killed it at 15 s.
- By reading resolve.h: the lock order is `Lookup::mutex_` -> `Owner::mutex` (a delivery may call
  start()). abandon() and the destructor never hold both mutexes at once, so this quest adds no
  lock inversion. Each running thread that is not busy takes one queued lookup under the same
  mutex before it can exit. The spawn test `running >= queue + busy` therefore never leaves a
  queued lookup with no thread to take it. `running + exited <= bound` holds on every path.

Answers to the lead's questions:

1. **Lifetime.**
   - A running lookup's delivery runs under `mutex_`. abandon() clears it under that mutex, so it
     either waits for a delivery already in progress or prevents it.
   - A queued lookup leaves the queue with its closures. The test checks this with weak_ptrs, and
     stress.cc confirms it under contention.
   - `Owner` is shared with each lookup, so abandon() after destruction is safe.
   - The one gap is that no test forces abandon() to land during a delivery (F2).
2. **Exit.**
   - The bounded wait is documented in peer-dns.md and in resolve.h:157. It is a user-visible
     behavior change; see N2.
   - A lookup started while the destructor runs throws `operation_canceled`, which Links and Peers
     catch.
   - A lookup started after the destructor returns is a use-after-free (F1).
   - `std::exit` on a lookup thread deadlocks (N1).
3. **Bound and queue.**
   - The bound of 16 holds on every path, and 3 of 3 held under contention.
   - The queue is FIFO and no lookup is lost. A lookup can wait in the queue only while every
     thread is parked in a resolver, which is the accepted cost (N3).
   - FIFO is enough. Each Peers instance has at most one lookup per name, and abandon() empties the
     queue.
4. **Proof.**
   - The tests check values: OS thread counts, whether closures were freed, and the addresses
     delivered.
   - Each mutant targets the claim it names.
   - The interleaving of abandon() with an in-flight delivery has no test that fails without it
     (F2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | `LookupThreads::process()` is a destructible function-local static. A Lookup::start that runs after its destructor returns dereferences the freed `owner_`. Examples are a reactor or Worker thread still dialing when the process exits, or a static destroyed later. On main the same call started a detached thread. With this commit it is a use-after-free at exit. | 261118-052437-4b7723 (ASan heap-use-after-free); resolve.h:164-167, 208 | Make the process instance immortal, for example `static auto &threads = *new LookupThreads(process_bound);`. Do its close-and-join from an atexit handler registered when it is built, so a later start() finds `closing` and throws `operation_canceled`. Test: a resolve_test case whose atexit handler is registered before the first Lookup::start, calls Lookup::start, and requires `std::system_error`. Add a `--fails-without` job against the current static. |
| F2 | yes | gap | The interleaving "abandon() while a delivery is in progress waits for it to return" has no test that fails without it. It is the property that keeps a Peers/Links destructor from racing a post to its executor, and resolve.h:83-84 and 253-254 name it. With the delivery moved outside `lookup->mutex_`, the whole resolve_test still passes. | 261118-052436-e9dbb9 (mutant not caught by resolve_test; caught by the stress probe) | Add a deterministic case: a delivery parks on a gate, and abandon() on another thread must not return until the gate opens. Optionally fold in stress.cc as a TSan property case. Add a `--fails-without` job using the delivery-outside-lock mutant in `run.sh stress`. |
| N1 | no | defect | `std::exit` called on a lookup thread (from a delivery or a resolver) hangs forever. ~LookupThreads waits for `running` to empty, and the calling thread is in `running`. Detent's own deliveries only post, so they cannot do this. | 261118-052437-4b7723 (killed at 15 s); resolve.h:186 | Document it in the "must" list for resolvers and deliveries, or skip the wait when the current thread is a lookup thread. Owner's call. |
| N2 | no | gap | Exit now waits for every running resolver: up to the system resolver's timeout for getaddrinfo, and without limit for an injected resolver that never returns. peer-dns.md documents this. CHANGELOG.md is outside the file set and does not mention it. | peer-dns.md, "The process's LookupThreads is destroyed at exit" | Keep it (it is the A2 contract) and add a CHANGELOG line once the file set allows. Alternatively, have exit wait only up to a bound. Owner's call. |
| N3 | no | nit | Within one resolver timeout, 16 restarts over a single blackholed name park all 16 threads. First lookups of every other name in the process then wait, although cached names still dial. This is ruling 2's accepted cost. | resolve.h:216; peer-dns.md "only 16 hung at once stall" | Optional: coalesce in-flight lookups by host, so that a new start of a name whose abandoned lookup is still running reuses that lookup. |
| N4 | no | nit | If `running.emplace` throws bad_alloc, the lookup stays queued and the caller never receives its handle, so the lookup later runs and delivers to a caller that believes start() failed. | resolve.h:221 | Wrap the emplace in the same erase-from-queue path as the std::thread failure. |
| N5 | no | nit | A child process forked without exec that calls exit() while a lookup was busy at fork time waits forever on threads that do not exist in the child. | resolve.h:186 | Note it in peer-dns.md, or use a pthread_atfork reset. Only relevant if a consumer forks. |
| N6 | no | nit | peer-dns.md's last paragraph has a ragged wrap: "(a\nbound of 1), a\nrefused address". | docs/mechanisms/peer-dns.md | Reflow. |

## Not run

- resolve_test under the CMake suite and the Linux TSan CI. It is not registered until
  consensus-close-fenced lands; that is known and pending.
- Linux and FreeBSD runs of resolve_test and of my probes, including the os_threads() paths on
  those platforms. Every probe ran on macOS clang.
- peers_test and links_test under contention with the process bound. I relied on CI and on
  261118-045523-10aced.
