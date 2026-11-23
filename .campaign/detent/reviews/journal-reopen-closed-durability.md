# journal-reopen-closed review: durability

Commit: journal:d23c57bcf298779b932fadfc968efe4f47398a3f
Verdict: CHANGES
Date: 2026-11-18

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The approach is right: the hold, parking by position with `TurnLog::live` verifying each record
read back (kind, group, sequence), the table as the retired floor, and ordered reclaim. The
blocking findings are failure-path and multi-loop defects in the closed-entries-out-of-memory
change, plus a red CI.

## Evidence I relied on

All at journal:d23c57b.

- A9: 261118-055804-3d69f0 (seeds 5/116, 5/161, 7/219 pass), 055804-78be82 (200 seeds pass),
  055805-322172 (unordered reclaim fails at those seeds), 055803-42a124 / 055805-6844b0 (1.0 vs
  0.939 directory barriers per reclaimed segment). Enough: the fails-without job reproduces
  journal-faultfs's damage, and the fix removes it.
- A3: 261118-055802-799652, 16 seeds and 1,788 power cuts, 0 failures. I read the test
  (journal_test.cc:2005-2056): it checks values both ways (acknowledged is a prefix of
  recovered, recovered is a prefix of attempted), not only counts.
- A1/A2: 055802-5965d3, 055804-b5ad99 (TSan, macOS, one loop), and fails-without jobs
  055806-c71ad7, 055806-91a898, 055806-fd33bc.
- A4/A7/A8: 055802-8fadd5 (churn checks every recovered value), 055803-110ddf, 055743-0fc5ca,
  055745-5573e8. I did not re-derive the heap numbers.

## What I checked myself

Scripts are in `/Users/kronuz/code/Kronuz/Detent/.scratch/review-journal-reopen-closed-durability/`.
`run.sh` builds a probe `.cc` as an extra target against a checkout.

- 261118-063202-5ac0ed (`probe_table.cc`, branch and main via `--ref MAIN`):
  - `commit_failure`: a recover's `reopen_group` commit fails at its directory sync after
    `replace`, then a create lands in the same slot. Branch: the closed group's status goes
    `stored`, then `none`, and stays `none` after a Host reopen (LOST). Main: it stays `closed`
    and recovers `bundle-a1`.
  - `refused_release`: a fenced Store whose append was refused ends, and a new Store recovers
    the group in the live Host. Branch: the recover succeeds, then every append fails with
    "turn record exceeds a segment". Main: the recover is refused by the old guard.
- 261118-063451-fc94be (`probe_race.cc`, `-fsanitize=thread`): a 3-loop Host on a real
  directory, with two threads creating groups whose slots share one 64-slot word across the
  loop 0/1 boundary. TSan reports 2 data races, `Host::commit` host.h:473 (write) against
  `Host::stored_shard` host.h:408 (read).
- Read by hand: the permit fix (turn_driver.h:481-486, store.h:61-90, admission.h:95), the
  `retired_` removal (turn_driver.h:150-160, 231, 247, 390; host.h:417-425), park/live/forward
  (turn_log.h:217-270, 356-366, 414-431), reclaim (turn_log.h:436-450) against the IO ordering
  contract (completion io.h:249-254), and the CI run.

## Answers to the lead's questions

1. **Disk reads for closed entries.** On the success paths, a reopened group reads exactly its
   records:
   - a closed id restarts from the table's checkpoint record;
   - a released id reads its live positions back, and each record is verified as itself;
   - forwarding updates parked positions;
   - a crash between close and reopen recovers from the unchanged on-disk format.

   A failed table commit is the exception: it can drop a closed id from disk (F1).
2. **The permit fix.** Neither loses a write, and nothing is reported durable early:
   - The dropped operation's record stays in `write_` and is written and synced with its turn.
   - The operation itself is never completed (`done()` stays false and `result()` throws
     "has not completed").
   - `Store::settle`'s `durable()` wait resumes only after the barrier.
   - The other operations in the same turn complete (reopen_released checks a neighbor).

   Notes N1 and N2 cover the loose ends.
3. **A9's ordering.** Ordering is the correct fix:
   - Under the IO contract, removals without a barrier between them may land in either order.
   - With a barrier before each later removal, the removals that survive a power cut are a
     prefix, so the remaining segments stay contiguous at every cut point.
   - Recovery cannot tell a reclaimed gap from a lost live segment without weakening damage
     detection. A durable low-water mark would cost a write and a sync instead.
   - The extra barrier is needed only between two removals in one batch. 1.0 per segment is
     the floor for removing each segment individually.
4. **Forgetting `retired_`.** The table floor (base + 1) is what `forget` computed for every
   ordinary close. `forget` keeps the larger floors, and the floor only rises across
   incarnations. A stale `durable()` waiter holds its operation, which holds the Journal and
   therefore the hold, so no waiter outlives a release. That leaves no ABA through the floor.
   A different piece of per-incarnation state does leak into the reopened group: `refused_`
   (F4).
5. **Proof.**
   - The crash sweep and the mutants test values.
   - The sweep runs one group, with no retention or forwarding, and only power cuts on
     MemoryIO; A9's faultfs jobs cover reordering.
   - No test exercises an I/O error, as opposed to a cut, in a table commit (F1). None runs
     table transitions on two loops (F3), and none releases a refused group (F4).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A failed table commit can permanently drop a closed group. When `reopen_group`'s commit fails after its `replace`, for example on an EIO from the directory sync, `update()`'s rollback erases the entry, because `before` was closed. The file already holds it open. The next commit in that slot erases every non-closed file entry that memory does not hold, so the group disappears from the table. Its log records were forgotten at close, so its data is gone. Main keeps the entry, because memory wins. | host.h:461, host.h:470; probe 261118-063202-5ac0ed (branch LOST, main kept and recovered) | Keep the `before` entry resident (closed) on a failed update, so the next commit rewrites it closed, and drop it from memory only after a successful commit. Test: `commit_failure` in probe_table.cc as a host_test case, asserting the group is `closed` before and after a Host reopen and recovers its bundle. |
| F2 | yes | defect | CI is red. linux-tsan does not link `journal_journal_test`: the replaced `operator new` and `operator delete` (journal_test.cc:47-60) are defined twice alongside libclang_rt.tsan_cxx, which is the trap review.md names. A6 is not met. macos-arm64 failed because no runner was acquired, which is infrastructure; rerun it. | CI run 44319089645, job 134853045590 | Count heap bytes with `__sanitizer_malloc_hook` under TSan, or compile the replacement and the heap cases out under `__has_feature(thread_sanitizer)`. Get CI green at the fixed head. |
| F3 | yes | defect | `stored_` is a `std::vector<bool>`, so neighboring slots share a word, and loops run table transitions concurrently on their own threads (consensus host_transport.h:96). Reads and writes from two loops race, which is undefined behavior. A lost write leaves a slot's bit false, and the next commit then reads no shard and drops that slot's closed-only entries, the same loss as F1. The A2 TSan job runs one loop, so it cannot see this. | host.h:629, :473, :408; probe 261118-063451-fc94be (2 TSan races) | Use one byte per slot (`std::vector<char>`, or a `bool` member in a per-slot struct). Test: probe_race.cc as a TSan case with two loop threads. |
| F4 | yes | defect | A released group keeps its driver's `refused_` entry. `park` does not clear it, and only `forget` does. After a fenced Store's refused append, the next Store recovers the group in the live Host, but every append it makes fails with `length_error` until the Host reopens. The old incarnation's state reaches the new holder. Default options reach this: a 64 MiB batch does not fit a 64 MiB segment. | turn_driver.h:142-145, :498-499, :511-516; probe 261118-063202-5ac0ed (`refused_release`) | Clear the group's refusal in `TurnDriver::park`. This is safe because refused records never reached the log, and the hold guarantees that no waiter of the old holder remains. Test: `refused_release` as a journal_test case that appends after the recover. |
| N1 | no | nit | `start()` catches everything from `submitted()`, not only the fence's "storage permit unavailable". If a non-fence error occurred, its record would be written while its Store was not fenced and its operation was never told. | turn_driver.h:485 | Fence the operation's journal on a drop, or rethrow anything other than the tainted-permit refusal. |
| N2 | no | nit | A dropped operation is never completed. A caller that keeps it and polls `done()` waits forever, and while the caller holds it, it also keeps the hold. | turn_driver.h:485, store.h:64 | Document it in group-lifetime.md, or give the operation a fenced terminal state. |
| N3 | no | nit | If `park` throws inside `settle()`, the rest of the swapped `ended` list is lost. Those groups stay indexed, so they remain recoverable, but they are never parked. | host.h:345-351 | Requeue the remainder on a throw. |
| N4 | no | nit | Every commit now reads and decodes its shard before writing it. Each `status()` or `entry()` of a closed id, and `retired_floor` inside `await_ready` (under the sealers' lock), decodes the whole shard file. | host.h:405-425, :468-470 | Measure at Xapiand's ids per slot, or keep a per-slot cache of closed ids. |
| N5 | no | nit | The Cost section of group-lifetime.md still says `reopen_distinct` closes 100,000 ids and reports after 1,000 and 100,000. D118 changed this to 10,000 ids reported at 1,000, 5,000 and 10,000. | docs/mechanisms/group-lifetime.md:84-86 | Restate it per D118. |

## Not run

- No local TSan run of the reopen cases (055804-b5ad99 stands for macOS only) and no Linux
  sanitizers; those are CI's.
- No faultfs reruns: A9's jobs are accepted.
- No crash sweep across a table commit that fails with an I/O error, as opposed to a power
  cut, beyond probe F1.
- No multi-loop run of hold/settle/park. `opened_` and `released_` are per loop, and
  `released_` is guarded by its mutex; I checked these by reading only.
- `ranges_` (host.h:631) is read in `publish` and written in `group_io` without a lock. That
  is the same on main and is not introduced here. Probe F3 does not call `group_io`, so it
  did not exercise it.
- Heap figures for A4, A7 and A8 were not re-derived.
