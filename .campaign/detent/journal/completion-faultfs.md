# completion-faultfs: journal

State: waiting on jobs 261118-015625-700ac1 (A1/A4 build+run), 261118-015632-b920c8 (A2), 261118-015633-814eb0 (A3 mutant), 261118-015637-8e0096 (suite, A5)
Quest: .campaign/detent/quests/completion-faultfs.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code on quest/detent/completion-faultfs (completion:0b1302c, pushed). Waiting on the jobs above.
Then: fold any failure, fill the Evidence table from `campaign brief`, set "ready for review".

## Next

1. Read the jobs; fix a failing faultfs case (a test bug, never the model).
2. Confirm the A3 job's log shows "mutant built" and a "failed: ... whole" line.
3. Fill Evidence and Result; CI is the lead's push.

## Blocked on

Nothing blocks the work, but the lead should read this before A1 is judged: the quest's premise
does not match completion:d8b538c. test/sync_seam_test.cc and test/seam_support.h held no crash
or tear model: sync_seam_test was a call-log test (a logging File and IO, the default
sync_data_files order, the executor reaching the seam), and seam_support.h is a logging backend
and an operation for seam_test and asio_seam_test. There was no old crash case to move.
I did the quest's evident intent instead: the same primitives (SyncData, Sync through the
inline loop and the native queue, and the batch calls) now also run over faultfs and are
crashed at 64 seeds. A1 is therefore "no old case existed; these are the new ones" (map below).
Option B, if the lead meant something else: name the file that holds the model (journal's
MemoryIO and strata's CrashIO live in other repositories; sync_files_test's SyncModel is below).
Recommendation: accept A.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | jobs 261118-015625-700ac1, 261118-015637-8e0096 (map below: no old crash case existed) |
| A2 | job 261118-015632-b920c8; the exception is SyncModel below |
| A3 | job 261118-015633-814eb0 (SyncData skips sync_data_files) |
| A4 | the tests as written, job 261118-015625-700ac1; see "faultlab rules" |
| A5 | suite 261118-015637-8e0096; CI is the lead's |

### Old case to new case

| Old (completion:d8b538c) | New |
|---|---|
| sync_seam_test defaults, posix, executor (call logs: order, errno, no File call) | Kept as they are: they assert calls on a logging IO, not storage state; they are not a crash model |
| seam_test held_drop (a primitive forgotten, the operation stays in flight) | Kept: executor behavior with no storage |
| none (no storage crash case existed for the seam) | crash(): SyncData and Sync, per route: unsynced bytes lost (size durable) and torn under the default policy; synced bytes whole at 64 seeds; one device sync; a later write and a second file stay unsynced; a sync leaves the name non-durable until sync_directory; a failed sync (keep) settles with EIO, stops the next step, makes nothing durable and a retry does not resurrect it; power off before the sync fails it and nothing is durable; power off after it lands |
| none | crash_batches(): sync_data_files and sync_files make the batch durable and not a file outside it; a failing first sync stops the batch |

### faultlab rules (A4)

1. A sync does not make mapped stores durable: the tests map nothing (faultfs `map_shared` is not used), so the rule is not exercised; no case needs it.
2. No folds after the power cut: the power-cut cases only check durable bytes after `crash`; no mapped store exists to fold.
3. Two versions only: the policies used are the default coin and `{names durable, size durable}`; no append size between versions is asserted (the default case asserts only that the payload appears among the many states, never a particular tear).

Cases that need more than faultfs: `sync_files_test.cc`'s SyncModel (staged, durable and ordered flags per device) models fsync, F_FULLFSYNC and F_BARRIERFSYNC per descriptor under `files_sync_devices` in posix.h, below the File seam where faultfs sits, and checks device grouping and barrier counts, not crash states. It stays; A2's grep excludes that file for it. Not faked.

## Result

(Filled when ready for review.)

## Rulings I made

1. faultlab pin is a452a8c5e796 (full hash in CMakeLists.txt), per the Xapiand lead's message, not 5ec8a9e: it reuses completion's own target when defined. FetchContent_MakeAvailable(faultlab) sits inside `if(BUILD_TESTING)`, top-level only, and links to completion_sync_seam_test alone: test-only, no cycle.
2. The faultfs cases run first in main, so the A3 mutant fails there, not in a call-log case.
3. The A3 mutant makes the executor's SyncData skip `sync_data_files`; the job passes only when the failure message names a faultfs case ("whole"), exit 127 on a build failure.
4. A2's grep is a pattern for crash and tear state names (durable_, staged_, torn_, unsynced_, ... and Crash/Torn/Model classes), not the word "crash" (seam_test's held_drop says it of an executor).
