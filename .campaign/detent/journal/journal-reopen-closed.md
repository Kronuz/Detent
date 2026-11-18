# journal-reopen-closed: journal

State: waiting on jobs 261118-023256-09c8e0 261118-023259-0f02eb 261118-023301-3faef9 261118-023315-c41188 261118-023317-9b8ff5 261118-023318-f912a3 261118-023320-db2f55 261118-023322-636173
Quest: .campaign/detent/quests/journal-reopen-closed.md (the lead's; never edit it)

## Doing

First implementation at journal:38de42f, pushed; jobs queued (none run yet, so it has not compiled yet).

## Next

1. Read the jobs; fix compile or test failures.
2. When journal-faultfs lands and host_test.cc is mine (lead, after msg-70): merge main, rewrite host_test second_recovery() to the new rule (it asserts the old Corruption guard; expected red in the suite until then).
3. Ask the lead for ctest entries for the long reopen cases (crash, churn, heap) once CMakeLists is free; today they run only by name as jobs (journal_journal has a 30 s timeout).
4. Durability review.

## Blocked on

Nothing for code. host_test.cc second_recovery() waits on journal-faultfs landing (lead ruling on msg-70).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-023256-09c8e0 (reopen cases); 261118-023318-f912a3 (fails-without: the old guard restored) |
| A2 | 261118-023256-09c8e0; 261118-023317-9b8ff5 (TSan); 261118-023320-db2f55 (fails-without: no hold) |
| A3 | 261118-023259-0f02eb (reopen_crash: 16 seeds, every power cut) |
| A4 | 261118-023301-3faef9 (reopen_churn: heap after first and last pass) |
| A4b | 261118-023315-c41188 (reopen_heap, tagged A4: the tool does not parse A4b) |
| A5 | [artifact] `docs/mechanisms/group-lifetime.md` (journal:38de42f) |
| A6 | 261118-023322-636173 (suite; host_test second_recovery expected red, see Blocked on) |

Messages: msg-68 (mine to lead: close_group already releases; asked turn_log.h), msg-69 (asked turn_log.h, turn_driver.h: granted), msg-70 (host_test.cc conflict: lead ruled, lands after journal-faultfs).

## Result

Not yet measured.

- Why the guard existed: the clean path (Store::close -> close_group -> TurnDriver::forget) already released the id; shared_store_test cycle() and the new reopen_clean_close show it. The guard bit ids never closed into the table (consensus's Driver drops the Store; a fenced Store cannot close): after a Store recovered a group, the driver released its records from memory and nothing could read them back, and nothing stopped a second Store. Per-id Host state was correct; it was unreadable, not double-counted.
- Fix: Host::hold (one holder per id; EWOULDBLOCK while held or while its last Store's records are staged); the hold ends at Journal::close or the Journal's end. Host::settle parks released ids (TurnLog::park keeps only live-record positions; retention still forwards them). recover_shared reads a released id's records back by position (TurnLog::live).
- Docs: docs/mechanisms/group-lifetime.md.

## Rulings I made

1. The hold lives in the Journal, not the OwnerSession: escaped readers (read-only, immutable artifacts) do not keep an id from being recovered; every writer path (StoreAppend, artifact and publication operations) holds the Journal, so it does keep the hold.
2. Busy is std::system_error(EWOULDBLOCK), the IO contract's held-owner-lock error, not Corruption: it is transient and the caller retries.
3. Sealers do not block a hold: they are registered per owner on the driver, not by Stores (no Store registers one today), and an ordering check there would refuse a writer that seals before it recovers.
4. Table entries stay in memory for released ids: commit() rewrites a slot's shard from memory, so dropping an entry drops it on disk; paging the table is a separate design (stated in msg-69; lead said go ahead).
5. A4 reading: 10,000 cycles in total over 1,000 ids (10 per id), even ids closing into the table and odd ids checkpointing then releasing with one batch uncovered, so the live state per id is constant and a flat heap is the claim.
6. opened_ and the release queue are per loop, so settle() only parks its own loop's groups (TurnLog is loop-confined).
7. Heap is measured with a replaced operator new in journal_test that leaves out allocations made inside the memory disk's calls (MeteredIO), so the numbers are the host's state, not MemoryIO's leaked inodes.
