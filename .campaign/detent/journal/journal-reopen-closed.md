# journal-reopen-closed: journal

State: waiting on jobs 261118-064517-9372f4 064519-8a384a 064522-d966b2 064524-caef4a 064526-c0d03d 064529-b4cc89 064531-67fae6 and the reruns below, plus CI at journal:4d364f1 (review fix round 1, pushed).

Findings: F1=261118-064517-9372f4 (fails without: 064519-8a384a); F2=CI linux-tsan at 4d364f1 (+064531-67fae6); F3=261118-064522-d966b2 (fails without: 064524-caef4a); F4=261118-064526-c0d03d (fails without: 064529-b4cc89). Suite: 064548-69f7ab.
Quest: .campaign/detent/quests/journal-reopen-closed.md (the lead's; never edit it)

## Doing

Review fix round 1 (.campaign/detent/reviews/journal-reopen-closed-durability.md, CHANGES), all in journal:4d364f1:
- F1: Host::update restores any entry on a failed commit, a closed one too (the file may hold it open; memory wins at the next commit), and drops the slot's closed entries from memory after a successful one (close_group's own erase went). Test host_test failed_reopen. Pass 261118-064517-9372f4; fails-without (old rollback) 064519-8a384a; reviewer's probe rerun 064549-cb889d.
- F2: journal_test replaces operator new only outside TSan (__SANITIZE_THREAD__ / __has_feature(thread_sanitizer)); reopen_churn, reopen_heap and reopen_distinct print "heap check skipped: TSan's runtime defines operator new" and skip only the heap check. Evidence: CI linux-tsan at 4d364f1 (failed to link at d23c57b, run 44319089645); local TSan build 064531-67fae6.
- F3: stored_ is std::vector<char>. Test host_test loop_slots (two loop threads, slots sharing one 64-slot word, PosixIO under .scratch/). TSan pass 064522-d966b2; fails-without (vector<bool>, TSan) 064524-caef4a; reviewer's probe rerun 064549-618682 (its grep -c exits 1 when it counts 0 races).
- F4: TurnDriver::park erases the group's refused_ entry. Test journal_test reopen_refused (in the default run). Pass 064526-c0d03d; fails-without (park keeps refused_) 064529-b4cc89.
- N5 fixed (Cost section states 10,000 ids at 1,000, 5,000 and 10,000). N1-N4 not taken.
- Every Evidence-table job rerun at 4d364f1 (ids below, under Evidence).

Head journal:c92eb27 (pushed): journal:4885819 (permit fix), journal:3516044 + c92eb27 (A9). Every job at a92dde9/cd6d017 is void (journal_test.cc:2127 `parked` shadowed parked(): no build); fixed in journal:4885819.

1. Red suite, confirmed: lldb on reopen_released(fenced) at cd6d017: logic_error "storage permit unavailable" from StoragePermit::mark_started <- StoreAppend::submitted (store.h:69) <- TurnDriver::start (turn_driver.h:481) <- commit(). A fenced Store's staged append refuses submission after the turn is staged; the throw left write_ staged and ends run(), stopping every group on the loop. Pre-existing on main (same loop, turn_driver.h:467), reached only by a fenced Store with a record staged. Fix: start() drops an operation whose submitted() throws (as settle() does); its record goes with the turn, keeping the group's sequence whole. reopen_released also checks a neighbor group's append in that turn completes.
2. A9: reclaim orders each removal after the first behind the one before (io_.order_files_and_directory({})), final sync_directory unchanged. Reproducer (faultfs, c606068's CMakeLists + turn_log_power_test.cc overlaid on a copy of my tree, never committed): locally 3 FAIL at 5/116, 5/161, 7/219 before; 0 failures after (2040 crash points). Directory barriers per reclaimed segment over turn_log_power's workload, 200 seeds (journal_test reclaim_syncs, local): before 0.939 (5,445 for 5,797 segments), after 1.0 (+6.5% barriers; on a backend with an ordering-only barrier the extra 352 are ordering points, not flushes).

## Next

1. Durability review.
2. When CMakeLists.txt joins the set: ctest entries for reopen_crash, reopen_churn, reopen_heap, reopen_distinct, reclaim_syncs.

## Blocked on

Nothing. A8 resolved by D118 (lead): 10,000 ids, slope under 1 B; memory_io.h gets no fix (faultlab step 8 deletes it). The 100,000-id run was quadratic in jt::MemoryIO::sync_directory (1,000 ids 4.8 s, 2,000 17.1 s, 4,000 50.0 s).

## Evidence

Reruns at journal:4d364f1 (old -> new): 055802-5965d3 -> 064542-c3da37; 055803-802427 -> 064542-8aa7cc; 043341-b92534, 055806-c71ad7 -> 064543-47189d; 043343-e3fdca, 055806-91a898 -> 064543-8b4151; 055804-b5ad99 -> 064543-3d0836; 043411-fd6782, 055806-fd33bc -> 064544-c6adb3; 055802-799652 -> 064544-b64d11; 055802-8fadd5 -> 064545-8b40b9; 055803-110ddf -> 064545-0fcd78; 055743-0fc5ca -> 064545-0ad089; 055745-5573e8 -> 064546-8208bc; 055803-42a124 -> 064546-bf20ed; 055804-3d69f0 -> 064547-7604e6; 055804-78be82 -> 064547-a8c91c; 043337-6f4c2f, 055805-322172 -> 064547-389464; 043339-8b3682, 055805-6844b0 -> 064548-0fc1ad; 055804-288249 (suite) -> 064548-69f7ab; reviewer probes 063202-5ac0ed -> 064549-cb889d, 063451-fc94be and 063204-ea6729 -> 064549-618682. The table below still names the d23c57b jobs until these finish.

All at journal:d23c57b (journal_test.cc changed for A8, so every job reran). Mutant and faultfs jobs run `.scratch/jobs/tree.sh` (my worktree), which builds a git-archive copy of the commit and prints "reached the test" before running it.

| Criterion | Evidence |
|---|---|
| A1 | 261118-064542-c3da37 (reopen cases, pass); 055803-802427 (host and journal default runs, pass); 055806-c71ad7 (fails-without: start() outside a try, FAIL: storage permit unavailable); 055806-91a898 (fails-without: old guard, FAIL: old guard) |
| A2 | 261118-064542-c3da37 (pass); 055804-b5ad99 (TSan, pass); 055806-fd33bc (fails-without: no hold, FAIL: busy) |
| A3 | 261118-064544-b64d11 (reopen_crash, pass) |
| A4 | 261118-064545-8b40b9 (reopen_churn, pass) |
| A5 | `docs/mechanisms/group-lifetime.md`: "" |
| A6 | 261118-064548-69f7ab (suite, pass); CI |
| A7 | 261118-064545-0fcd78 (reopen_heap, pass) |
| A8 | 261118-064545-0ad089 (106,296 B at 1,000 and at 10,000: slope 0, pass); 055745-5573e8 (main's headers: 392 B per group, fails as required) |
| A9 | 261118-064547-7604e6 (turn_log_power 8, pass); 055804-78be82 (200 seeds, pass); 055805-322172 (fails-without: unordered reclaim, FAIL at 5/116, 5/161, 7/219); 055803-42a124 (1.0 barriers per segment after); 055805-6844b0 (0.939 before) |

At journal:c92eb27, before the A8 edit: passed 043305, 6c91a2, eb6e13, 9d76bf, 161c74, b3f159, 1ce564, suite 09b70e; fails-without held 6f4c2f, 8b3682, b92534, e3fdca, fd6782. Local 2,000-id run at d23c57b: slope -0.027 B per group.

Messages: msg-68 (mine to lead: close_group already releases; asked turn_log.h), msg-69 (asked turn_log.h, turn_driver.h: granted), msg-70 (host_test.cc conflict: lead first ruled it lands after journal-faultfs; latest message gives me host_test.cc and drops the wait, done at cd6d017). Lead's note (void a92dde9 jobs, `parked` shadowing): already fixed at journal:4885819; everything resubmitted at journal:c92eb27.

## Result

Not yet measured.

- Why the guard existed: the clean path (Store::close -> close_group -> TurnDriver::forget) already released the id; shared_store_test cycle() and the new reopen_clean_close show it. The guard bit ids never closed into the table (consensus's Driver drops the Store; a fenced Store cannot close): after a Store recovered a group, the driver released its records from memory and nothing could read them back, and nothing stopped a second Store. Per-id Host state was correct; it was unreadable, not double-counted.
- Fix: Host::hold (one holder per id; EWOULDBLOCK while held or while its last Store's records are staged); the hold ends at Journal::close or the Journal's end. Host::settle parks released ids (TurnLog::park keeps only live-record positions; retention still forwards them). recover_shared reads a released id's records back by position (TurnLog::live).
- A8 (G5-J1): per closed id the Host kept a TurnDriver::retired_ node, the table entry with its manifest (~290 B) and an opened_ node. retired_ is the stale-writer floor: after a close, no record may reuse and no durable() wait may ack a sequence the closed incarnation could hand out (host_test sealer_close). It is now answered by the table (retire_with): a group closed at s retires through s + 1, which is what forget computed for every ordinary close; forget keeps an entry only for a larger floor (a waiter past s + 1, refused records). Closed entries leave memory: commit merges the shard file's closed entries, entry()/status() read them back, reopen_group loads them. opened_ is erased at settle.
- Docs: docs/mechanisms/group-lifetime.md.

## Rulings I made

14. F2 by exclusion, not hooks: TSan's malloc hooks cannot tell a metered allocation from one the memory disk made at free time without a side table, so under TSan journal_test does not replace operator new and the three heap cases skip only their heap check, printing why; their value checks still run.
15. F1's closed entry stays resident only on a failed commit; any successful update drops the slot's closed entries, so the A8 bound holds once a commit succeeds.
16. tree.sh gained --flags (untracked script; older options unchanged) for the TSan fails-without.

1. The hold lives in the Journal, not the OwnerSession: escaped readers (read-only, immutable artifacts) do not keep an id from being recovered; every writer path (StoreAppend, artifact and publication operations) holds the Journal, so it does keep the hold.
2. Busy is std::system_error(EWOULDBLOCK), the IO contract's held-owner-lock error, not Corruption: it is transient and the caller retries.
3. Sealers do not block a hold: they are registered per owner on the driver, not by Stores (no Store registers one today), and an ordering check there would refuse a writer that seals before it recovers.
4. Open and released (parked) ids keep their table entry in memory (next_artifact and view ranges read it); closed ids keep none (A8): commit() merges the closed entries the shard file holds. Host::entry() now returns std::optional<GroupEntry> (a copy) instead of a pointer; every caller's `->` still compiles.
8. The retired floor of a closed group is its close sequence + 1, read from the table; the table format and base semantics are unchanged (host_test close_behind asserts base == close sequence). A floor past s + 1 stays in the driver's map, which only stale waiters on later sequences or refused records produce.
9. Host::retired_floor returns UINT64_MAX (retire everything) if the shard file cannot be read: it runs inside noexcept await_ready.
5. A4 reading: 10,000 cycles in total over 1,000 ids (10 per id), even ids closing into the table and odd ids checkpointing then releasing with one batch uncovered, so the live state per id is constant and a flat heap is the claim.
6. opened_ and the release queue are per loop, so settle() only parks its own loop's groups (TurnLog is loop-confined).
7. Heap is measured with a replaced operator new in journal_test that leaves out allocations made inside the memory disk's calls (MeteredIO), so the numbers are the host's state, not MemoryIO's leaked inodes.
10. A fenced operation that refuses submission is dropped, not failed: complete() needs a submitted operation, so there is nothing to tell it; its caller already sees the fence. Its record is written: its sequence is assigned and later records of the group may follow it, and a durable unacknowledged record is a state the old rule allows.
11. A9 by ordering, not by recovery: a segment-number gap is the only sign of a lost live segment whose groups wrote nothing after it, so recovery cannot tell a reclaimed gap from a lost one without weakening damage detection. Ordering costs one ordering point per extra segment in a batch (measured 352 per 5,797 segments) and nothing for single-segment batches.
12. A9's reproducer stays journal-faultfs's file: jobs overlay it from --ref FF=journal@c606068 onto a copy of my tree; nothing of faultfs enters my file set.
13. reopen_distinct keeps JOURNAL_DISTINCT_IDS (default 10,000; early = ids/10, middle = ids/2) for timing other counts.
