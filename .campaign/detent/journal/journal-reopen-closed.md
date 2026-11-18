# journal-reopen-closed: journal

State: waiting on jobs 261118-031529-cb59bd 261118-031533-72cca5 261118-031535-47145a 261118-031537-366ec4 261118-031540-2fae9f 261118-031542-ff0b3c 261118-031545-7889bb 261118-031547-66d2d5 261118-031549-f7e40e 261118-031551-83ea4c 261118-040530-6f6b66. Context past the lead's threshold: handing off (A9 to a fresh contributor, per the lead).
Quest: .campaign/detent/quests/journal-reopen-closed.md (the lead's; never edit it)

## Doing

journal:cd6d017 (pushed) rewrites host_test second_recovery() to the one-holder rule (busy while the first Store lives, then the live host recovers the latest state; reopened host agrees). On top of journal:a92dde9 (A8) and journal:38de42f (hold/park/live).

Known red: suite job 261118-023322-636173 at journal:38de42f failed: journal_journal "FAIL: storage permit unavailable" (an uncaught std::logic_error from StoragePermit::mark_started, admission.h:96, permit tainted) and host_test's three old-rule second_recovery checks (now rewritten). Suspect: reopen_released(true) in journal_test.cc (~line 1917): `a->fence_storage(); staged.reset();` leaves a started-or-staged StoreAppend whose permit is tainted, and TurnDriver::start() calls item.operation->submitted() at turn_driver.h:481 outside any try, so the throw escapes the loop's pump. Unverified; 261118-031529-cb59bd (reopen cases at a92dde9) and 261118-040530-6f6b66 (default runs at cd6d017) will show which case. If confirmed, decide: (a) start() settles an item whose submitted() throws (fail that op only, as settle() at turn_driver.h:646 does) so one tainted Store cannot stop a loop, with a fails-without test; or (b) the test was wrong to fence that way. (a) looks right: a fenced Store must not break its neighbors.

## Next

1. Read the jobs above; fix the permit failure (see Doing) and any compile or test failure; rerun the A1-A8 jobs and the suite at the fixed head.
2. A9 (new, from the lead; recommended for a fresh contributor): TurnLog::reclaim (turn_log.h ~369-384) removes or recycles several segments then calls one sync_directory; a power cut can keep a later removal and drop an earlier one, so recovery sees a gap in segment numbers and sets damaged (turn_log.h ~560). No record is lost, but it reports damage. Reproducer: journal-faultfs job 261118-040056-965498's command at journal:c606068, `journal_turn_log_power_test 8`, seeds 5/116, 5/161, 7/219. Fix by ordering the removals (order_files_and_directory, oldest first) or by recovery telling a reclaimed gap (below the oldest live segment's first record / covered by retention) from a lost live segment, whichever is correct with fewer syncs. Report syncs per reclaimed segment before and after. Fails-without at those seeds; a 200-seed sweep. turn_log_power_test.cc is journal-faultfs's file: ask the lead before editing it; test from journal_test.cc or a job running faultfs's binary at a merged head.
3. When CMakeLists.txt joins the set: ctest entries for reopen_crash, reopen_churn, reopen_heap, reopen_distinct; until then they run by name as direct-build jobs.
4. Durability review.

## Blocked on

Nothing. host_test.cc is in the set and the quest no longer waits on journal-faultfs (lead, latest message).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261118-031529-cb59bd (reopen cases); 261118-040530-6f6b66 (host second_recovery, journal default run at cd6d017); 261118-031545-7889bb (fails-without: the old guard restored) |
| A2 | 261118-031529-cb59bd; 261118-031542-ff0b3c (TSan); 261118-031547-66d2d5 (fails-without: no hold) |
| A3 | 261118-031533-72cca5 (reopen_crash: 16 seeds, every power cut) |
| A4 | 261118-031535-47145a (reopen_churn: heap after first and last pass) |
| A5 | [artifact] `docs/mechanisms/group-lifetime.md` (journal:a92dde9) |
| A6 | 261118-031551-83ea4c (suite at a92dde9; host second_recovery rewritten after it, at cd6d017); 261118-023322-636173 failed at 38de42f (see Doing) |
| A9 | none yet (see Next 2) |
| A7 | 261118-031537-366ec4 (reopen_heap: bytes per released group before and after settle) |
| A8 | 261118-031540-2fae9f (reopen_distinct: heap after 1,000 and 100,000 closes); 261118-031549-f7e40e (fails at main: main's include/ with this test) |

Messages: msg-68 (mine to lead: close_group already releases; asked turn_log.h), msg-69 (asked turn_log.h, turn_driver.h: granted), msg-70 (host_test.cc conflict: lead first ruled it lands after journal-faultfs; latest message gives me host_test.cc and drops the wait, done at cd6d017).

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
