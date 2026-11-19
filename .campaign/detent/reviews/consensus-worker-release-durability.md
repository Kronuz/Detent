# consensus-worker-release review: durability

Commit: consensus:4fe7df25c02a53beac80b50feb1903e74ac3524f
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

None finished. The gate suite 261119-140158-51b1ce (A6) is well formed and still queued. The twelve
criterion jobs the journal lists (135117-bf02e4 through 135147-8ca554) are malformed (F1): they
prove nothing even when they run. I read the code at consensus:4fe7df2 and at journal:be8222c
(include/journal/host.h, journal.h, store.h, turn_driver.h, turn_log.h) and completion:d8b538c (io.h).

## What I checked myself

Scripts are in `.scratch/detent/review-consensus-worker-release-durability/` in the Detent home.
Both jobs were still queued when I wrote this:

- 261119-140902-4049e0 `failed_close_probe.sh`: a close whose table commit fails at the 1st, 2nd or
  3rd `sync_directory` after the shard file was replaced, then a live recover of the same id, an
  acknowledged record `x`, a later directory sync and a power cut. It checks that recovery keeps `x`
  (F2).
- 261119-140904-e57aba `proof.sh` (Q6) runs these:
  - reopen_rows, pool_bound and close_crash (32 seeds) at the branch.
  - reopen_rows under main's worker.h/driver.h (5935efa).
  - Mutants: table (no Store::close), settle, watch, nofence (failed close not fenced), nocover
    (no frontier pre-check; under reopen_rows and close_crash), and pool.

What reading established:

- Q1: "Covering every batch" is checked three times: `close_store` (worker.h:653), `Journal::close`
  (journal.h:359, plus a durability check) and `Host::close_group` (host.h:414, sequence >= the
  log's last). These checks do not take it on trust.
- Q1: Draining, `forget` and `settle` do no IO. The table commit's replace and sync are IO operations,
  so close_crash's cut-at-every-operation loop reaches each of them on a table close (action 2).
  `prefix(state, attempted)` catches a replayed covered record.
- Q1: The sweep cannot reach one class of failure. Its `jt::Disk` cut is permanent (memory_io.h:57),
  so a close that fails while the process lives on never happens there (F2).
- Q4: `watch()` agrees with journal's numbering. A reopened group stages from
  `max(base, retired)` (journal.h:605), and `TurnDriver::retired` is "the last sequence an earlier
  incarnation could hand out". The forget fails waiters with group_closed and marks the row ready,
  so no wake is lost.
- Q5: The repin changes no contract that consensus relies on. Consensus implements no production IO.
  The test IOs (worker_test.cc:45 FaultIO, driver_close_test.cc HeapIO) inherit completion's
  `sync_files` and `sync_data_files` defaults, which sync each file (io.h:276-283).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | All twelve criterion jobs (A1 bf02e4 b6b882, A2 52f24d 4b09cc, A3 1a5534, A5 88fd6c, mutants 89cbce f9a18c 11af82 860ad8 52f61f, A4 8ca554) are malformed. Each `cmd` starts with the one quoted token `'--quest consensus-worker-release'`, so `quest` is empty, no `--for` is set and `--fails-without` is not applied. They will exit 127. A1-A5 have no evidence, and the "fails at main" and mutant claims are unshown. | `campaign job show 261119-135119-b6b882` (`"quest": ""`, cmd as described) | Resubmit with the options before `--`. |
| F2 | yes | defect | **A failed close followed by a live reopen can lose acknowledged records.** Suppose `close_group`'s commit fails after `replace_file` (a `sync_directory` error, host.h:506-509). `update` then restores the entry to open in memory (host.h:488-492), but the shard file says closed with base=S. The Worker fences, the row is released, and the same id recovers live from the log, as ruling 3 and DRIVER.md promise. It then appends S+1 and onward. After any later directory sync and a power cut, the host opens with base=S, so records up to S are superseded and S+1 onward have no checkpoint. TurnLog marks the group lost (turn_log.h:658-660): acknowledged records are gone and the id is refused. Before this quest, consensus never ran `close_group`, so the quest introduced this path. A3 cannot reach it (permanent cuts only). | Analysis. Confirming probe 261119-140902-4049e0 is queued. | Either journal keeps a closed-in-doubt entry, or fails the host, on a failed close commit (as TurnLog fails on an unknown barrier). Or consensus refuses a live recover of an id whose close failed until the host reopens. Add the probe as a driver_close_test case that fails today. |
| F3 | no | gap | Ruling 3 ("a failed Store::close fences") has no test. No case injects a close failure, so the nofence mutant should survive. F2's test covers it. | e57aba (queued), reading | Folded into F2. |
| F4 | no | nit | `std::max(sequence, retired) + 1` wraps to 0 if `retired()` returns its "unreadable table" UINT64_MAX (host.h:456). That cannot happen for a resident open group. | driver.h:957 | Saturate, or leave as is. |
| F5 | no | nit | `close_store` fences on any `Store::close` logic_error ("a replacement is pending", "the last record is not durable yet") where releasing would do. No table write happens on those paths, so durability is safe. | worker.h:654-656, store.h:389-393 | Optional. |
| F6 | no | gap | Q6, by reading: reopen_rows pins the table, settle and watch mutants (the status-closed check, the parked check, and the loop's spin exit), and main's headers fail its status check. The nocover pre-check duplicates `Journal::close`'s own check, so its mutant should survive. | e57aba (queued) | None beyond F1. |

## Not run

- No job result: every job, mine and the contributor's, was queued when I wrote this.
- TSan (A4) and CI.
- closed_bytes heap numbers (A2).
- close_crash at 200 seeds.
- Owned (non-shared) Stores.
- Multi-voter rows.
- Concurrency of close with a cross-thread host `settle` beyond the one-loop tests.
