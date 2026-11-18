# journal-reopen-closed: journal

State: waiting on jobs 261118-043307-6c91a2 043309-eb6e13 043312-9d76bf 043314-4c29f5 043316-161c74 043318-473163 043320-b3f159 043332-1ce564 043334-531661 043337-6f4c2f 043339-8b3682 043341-b92534 043343-e3fdca 043356-9c13d7 043358-09b70e 043411-fd6782 (all at journal:c92eb27).
Quest: .campaign/detent/quests/journal-reopen-closed.md (the lead's; never edit it)

## Doing

Head journal:c92eb27 (pushed): journal:4885819 (permit fix), journal:3516044 + c92eb27 (A9). Every job at a92dde9/cd6d017 is void (journal_test.cc:2127 `parked` shadowed parked(): no build); fixed in journal:4885819.

1. Red suite, confirmed: lldb on reopen_released(fenced) at cd6d017: logic_error "storage permit unavailable" from StoragePermit::mark_started <- StoreAppend::submitted (store.h:69) <- TurnDriver::start (turn_driver.h:481) <- commit(). A fenced Store's staged append refuses submission after the turn is staged; the throw left write_ staged and ends run(), stopping every group on the loop. Pre-existing on main (same loop, turn_driver.h:467), reached only by a fenced Store with a record staged. Fix: start() drops an operation whose submitted() throws (as settle() does); its record goes with the turn, keeping the group's sequence whole. reopen_released also checks a neighbor group's append in that turn completes.
2. A9: reclaim orders each removal after the first behind the one before (io_.order_files_and_directory({})), final sync_directory unchanged. Reproducer (faultfs, c606068's CMakeLists + turn_log_power_test.cc overlaid on a copy of my tree, never committed): locally 3 FAIL at 5/116, 5/161, 7/219 before; 0 failures after (2040 crash points). Directory barriers per reclaimed segment over turn_log_power's workload, 200 seeds (journal_test reclaim_syncs, local): before 0.939 (5,445 for 5,797 segments), after 1.0 (+6.5% barriers; on a backend with an ordering-only barrier the extra 352 are ordering points, not flushes).

## Next

1. Read the jobs in State; fold any failure.
2. Lead: cancel 261118-043345-74c02a (invalid: its literal mutation matches twice, exits 124; 043411-fd6782 replaces it). My cancel was refused by the permission classifier.
3. When CMakeLists.txt joins the set: ctest entries for reopen_crash, reopen_churn, reopen_heap, reopen_distinct, reclaim_syncs.
4. Durability review.

## Blocked on

Nothing. host_test.cc is in the set and the quest no longer waits on journal-faultfs (lead, latest message).

## Evidence

All at journal:c92eb27. Jobs at a92dde9/cd6d017 are void (no build). Mutant and faultfs jobs run `.scratch/jobs/tree.sh` (my worktree), which builds a git-archive copy of the commit, so the checkout stays clean.

| Criterion | Evidence |
|---|---|
| A1 | 261118-043305-84c658 (reopen cases, done); 043320-b3f159 (host and journal default runs); 043341-b92534 (fails-without: start() submits outside a try); 043343-e3fdca (fails-without: the old guard) |
| A2 | 261118-043305-84c658; 043318-473163 (TSan); 043411-fd6782 (fails-without: no hold) |
| A3 | 261118-043307-6c91a2 (reopen_crash) |
| A4 | 261118-043309-eb6e13 (reopen_churn) |
| A5 | [artifact] `docs/mechanisms/group-lifetime.md` (journal:a92dde9) |
| A6 | 261118-043358-09b70e (suite); CI |
| A7 | 261118-043312-9d76bf (reopen_heap) |
| A8 | 261118-043314-4c29f5 (reopen_distinct); 043356-9c13d7 (fails at main's headers) |
| A9 | 261118-043332-1ce564 (turn_log_power 8 at head, faultfs overlay --ref c606068); 043334-531661 (200 seeds); 043337-6f4c2f (fails-without: unordered reclaim, FAIL at seed 5 cut 116); 043316-161c74 (barriers per segment after: 1.0); 043339-8b3682 (before: 0.939) |

Messages: msg-68 (mine to lead: close_group already releases; asked turn_log.h), msg-69 (asked turn_log.h, turn_driver.h: granted), msg-70 (host_test.cc conflict: lead first ruled it lands after journal-faultfs; latest message gives me host_test.cc and drops the wait, done at cd6d017). Lead's note (void a92dde9 jobs, `parked` shadowing): already fixed at journal:4885819; everything resubmitted at journal:c92eb27.

## Result

Not yet measured.

- Why the guard existed: the clean path (Store::close -> close_group -> TurnDriver::forget) already released the id; shared_store_test cycle() and the new reopen_clean_close show it. The guard bit ids never closed into the table (consensus's Driver drops the Store; a fenced Store cannot close): after a Store recovered a group, the driver released its records from memory and nothing could read them back, and nothing stopped a second Store. Per-id Host state was correct; it was unreadable, not double-counted.
- Fix: Host::hold (one holder per id; EWOULDBLOCK while held or while its last Store's records are staged); the hold ends at Journal::close or the Journal's end. Host::settle parks released ids (TurnLog::park keeps only live-record positions; retention still forwards them). recover_shared reads a released id's records back by position (TurnLog::live).
- A8 (G5-J1): per closed id the Host kept a TurnDriver::retired_ node, the table entry with its manifest (~290 B) and an opened_ node. retired_ is the stale-writer floor: after a close, no record may reuse and no durable() wait may ack a sequence the closed incarnation could hand out (host_test sealer_close). It is now answered by the table (retire_with): a group closed at s retires through s + 1, which is what forget computed for every ordinary close; forget keeps an entry only for a larger floor (a waiter past s + 1, refused records). Closed entries leave memory: commit merges the shard file's closed entries, entry()/status() read them back, reopen_group loads them. opened_ is erased at settle.
- Docs: docs/mechanisms/group-lifetime.md.

## Rulings I made

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
